/**
 * @file Relay.c
 * @brief Listener and per-connection workers that forward bytes, late.
 */

#include <WinSock2.h>
#include <WS2tcpip.h>
#include <Windows.h>

#include "Relay.h"
#include "Sockets.h"

/** Slots in the array handed to WSAWaitForMultipleEvents. */
#define WAIT_SLOT_ABORT 0
#define WAIT_SLOT_CLIENT 1
#define WAIT_SLOT_TARGET 2
#define WAIT_SLOT_MAX 3

/** Bytes in a chunk before the payload. abData is a header, not an array. */
#define CHUNK_HEADER_CB (FIELD_OFFSET(DELAY_CHUNK, abData))

/**
 * @brief One buffer of data waiting out its delay.
 *
 * @details Allocated to fit the bytes actually read, so a stream of small
 * writes does not cost a full chunk each.
 */
typedef struct _DELAY_CHUNK
{
    struct _DELAY_CHUNK *pNext;      /**< Next chunk, later or equally due */
    ULONGLONG            ullDueTick; /**< GetTickCount64 value to send after */
    DWORD                cbData;     /**< Bytes held */
    DWORD                cbSent;     /**< Bytes already written to the sink */
    BYTE                 abData[1];  /**< Payload; cbData bytes follow */

} DELAY_CHUNK, *PDELAY_CHUNK;

/**
 * @brief One half of a relay: a source, a sink, and the queue between them.
 *
 * @details The two readable and writable flags exist because FD_READ and
 * FD_WRITE are edge triggered. Winsock re-arms them only after the matching
 * call has failed with WSAEWOULDBLOCK, so the code has to remember that it is
 * allowed to try rather than waiting for a notification that will not come.
 */
typedef struct _RELAY_DIR
{
    PSOCKET_CONN pSource;         /**< Where bytes come from */
    PSOCKET_CONN pSink;           /**< Where bytes go */
    PDELAY_CHUNK pHead;           /**< Oldest queued chunk */
    PDELAY_CHUNK pTail;           /**< Newest queued chunk */
    DWORD        cbQueued;        /**< Bytes held, for back pressure */
    DWORD        dwLatencyMs;     /**< Delay added to each chunk */
    BOOL         bSourceReadable; /**< A recv is worth attempting */
    BOOL         bSinkWritable;   /**< A send is worth attempting */
    BOOL         bSourceClosed;   /**< Source sent FIN */
    BOOL         bSinkShutdown;   /**< We sent FIN to the sink */
    BOOL         bFailed;         /**< Direction is unusable */

} RELAY_DIR, *PRELAY_DIR;

/** @brief Per-client state handed to a session thread. */
typedef struct _RELAY_SESSION
{
    SOCKET_CONN  Client; /**< Accepted connection */
    SOCKET_CONN  Target; /**< Our connection to the target */
    RELAY_CONFIG Config; /**< Copy taken at start */

} RELAY_SESSION, *PRELAY_SESSION;

static RELAY_CONFIG      g_Config                         = {0};
static PSOCKET_CTX       g_pListener                      = NULL;
static HANDLE            g_hAbort                         = NULL;
static HANDLE            g_hAcceptThread                  = NULL;
static HANDLE            g_ahSessions[RELAY_MAX_SESSIONS] = {0};
static CRITICAL_SECTION  g_SessionLock                    = {0};
static BOOL              g_bSessionLockReady              = FALSE;
static volatile LONG     g_lRunning                       = 0;
static RELAY_NOTIFY_PROC g_pfnNotify                      = NULL;
static PVOID             g_pNotifyContext                 = NULL;

/**
 * @brief Reports a notification through the registered callback, if any.
 *
 * @param Code  What happened.
 * @param lData Code specific payload.
 */
static VOID Notify(_In_ RELAY_NOTIFY Code, _In_ LPARAM lData);

/**
 * @brief Accepts clients until the abort event is signalled.
 *
 * @param lpParameter Unused.
 *
 * @return Always 0.
 */
static DWORD WINAPI AcceptThreadProc(_In_ LPVOID lpParameter);

/**
 * @brief Connects to the target and pumps one client to completion.
 *
 * @param lpParameter PRELAY_SESSION. Owned and freed by this thread.
 *
 * @return Always 0.
 */
static DWORD WINAPI SessionThreadProc(_In_ LPVOID lpParameter);

/**
 * @brief Runs both directions until they finish or the relay aborts.
 *
 * @param pSession Session with both connections open.
 */
static VOID SessionPump(_In_ PRELAY_SESSION pSession);

/**
 * @brief Reaps finished sessions and claims a free slot.
 *
 * @return Index of the claimed slot, or -1 when the table is full.
 */
static LONG SessionTableClaim(VOID);

/**
 * @brief Puts a thread handle into a claimed slot, or releases it.
 *
 * @param lIndex  Slot returned by SessionTableClaim.
 * @param hThread Handle to store. NULL releases the slot instead.
 */
static VOID SessionTableStore(_In_ LONG lIndex, _In_opt_ HANDLE hThread);

/**
 * @brief Waits for every live session thread and closes its handle.
 */
static VOID SessionTableJoinAll(VOID);

/**
 * @brief Prepares a direction for use.
 *
 * @param pDir        Direction to fill in.
 * @param pSource     Connection to read from.
 * @param pSink       Connection to write to.
 * @param dwLatencyMs Delay to apply to each chunk.
 */
static VOID DirectionInit(_Out_ PRELAY_DIR  pDir,
                          _In_ PSOCKET_CONN pSource,
                          _In_ PSOCKET_CONN pSink,
                          _In_ DWORD        dwLatencyMs);

/**
 * @brief Frees anything still queued in a direction.
 *
 * @param pDir Direction to drain. May be NULL.
 */
static VOID DirectionFree(_Inout_opt_ PRELAY_DIR pDir);

/**
 * @brief Records the FD_ bits that fired against a direction.
 *
 * @param pDir          Direction to update. May be NULL.
 * @param lSourceEvents Events reported by the source connection.
 * @param lSinkEvents   Events reported by the sink connection.
 */
static VOID
DirectionApplyEvents(_Inout_opt_ PRELAY_DIR pDir, _In_ LONG lSourceEvents, _In_ LONG lSinkEvents);

/**
 * @brief Drains the source into the queue while it has data to give.
 *
 * @param pDir      Direction to read into. May be NULL.
 * @param pbScratch Working buffer. May be NULL.
 * @param cbScratch Size of pbScratch in bytes.
 */
static VOID DirectionRead(_Inout_opt_ PRELAY_DIR                  pDir,
                          _Out_writes_bytes_opt_(cbScratch) PBYTE pbScratch,
                          _In_ DWORD                              cbScratch);

/**
 * @brief Sends every queued chunk whose delay has expired.
 *
 * @param pDir Direction to write from. May be NULL.
 */
static VOID DirectionWrite(_Inout_opt_ PRELAY_DIR pDir);

/**
 * @brief Half-closes the sink once the source is done and the queue is empty.
 *
 * @param pDir Direction to finish. May be NULL.
 */
static VOID DirectionFinish(_Inout_opt_ PRELAY_DIR pDir);

/**
 * @brief Delivers whatever is still queued, then half-closes the sink.
 *
 * @param pDir   Direction to flush. May be NULL.
 * @param hAbort Event that cuts the flush short. May be NULL.
 */
static VOID DirectionDrain(_Inout_opt_ PRELAY_DIR pDir, _In_opt_ HANDLE hAbort);

/**
 * @brief Reports whether a direction has nothing left to do.
 *
 * @param pDir Direction to test. May be NULL.
 *
 * @return TRUE when the direction is closed or broken.
 */
static BOOL DirectionIsDone(_In_opt_ CONST RELAY_DIR *pDir);

/**
 * @brief Milliseconds until this direction next needs attention.
 *
 * @param pDir Direction to measure. May be NULL.
 *
 * @return A wait in milliseconds, or INFINITE when only an event can help.
 */
static DWORD DirectionWaitMs(_In_opt_ CONST RELAY_DIR *pDir);

SOCKET_STATUS
RelayStart(_In_ CONST RELAY_CONFIG   *pConfig,
           _In_opt_ RELAY_NOTIFY_PROC pfnNotify,
           _In_opt_ PVOID             pNotifyContext)
{
    SOCKET_STATUS SockStatus = SOCKET_STATUS_ERROR;
    DWORD         dwThreadId = 0;
    BOOL          bOwnsState = FALSE;

    if (NULL == pConfig)
    {
        SockStatus = SOCKET_STATUS_INVALID_ARGS;
        goto EXIT_RELAY_START;
    }

    if (0 != InterlockedCompareExchange(&g_lRunning, 0, 0))
    {
        SockStatus = SOCKET_STATUS_ERROR;
        goto EXIT_RELAY_START;
    }

    /* Past this point the module state is ours, so a failure below is allowed
       to clean it up. Before it, a failure must leave a relay that is already
       running completely alone. */
    bOwnsState = TRUE;

    CopyMemory(&g_Config, pConfig, sizeof(RELAY_CONFIG));
    g_pfnNotify      = pfnNotify;
    g_pNotifyContext = pNotifyContext;

    /* Before the listener takes the port, so a target that does not resolve
       fails Start rather than looking healthy until a client connects. */
    SockStatus = SocketResolveCheck(g_Config.wszTargetAddr, g_Config.wszTargetPort);

    if (SOCKET_IS_ERROR(SockStatus))
    {
        goto EXIT_RELAY_START;
    }

    InitializeCriticalSection(&g_SessionLock);
    g_bSessionLockReady = TRUE;

    /* Manual reset: once we are shutting down, every thread that looks at it
       has to see it, not just whichever one wakes first. */
    g_hAbort = CreateEventW(NULL, TRUE, FALSE, NULL);

    if (NULL == g_hAbort)
    {
        SockStatus = SOCKET_STATUS_CREATE_EVENT_FAIL;
        goto EXIT_RELAY_START;
    }

    SockStatus = SocketContextCreate(&g_pListener);

    if (SOCKET_IS_ERROR(SockStatus))
    {
        goto EXIT_RELAY_START;
    }

    SockStatus = SocketContextInit(g_pListener, g_Config.wszListenAddr, g_Config.wszListenPort);

    if (SOCKET_IS_ERROR(SockStatus))
    {
        goto EXIT_RELAY_START;
    }

    SockStatus = SocketBindAndListen(g_pListener);

    if (SOCKET_IS_ERROR(SockStatus))
    {
        goto EXIT_RELAY_START;
    }

    /* Marked running before the thread starts, so the thread never observes a
       relay that is listening but not yet officially up. */
    InterlockedExchange(&g_lRunning, 1);

    g_hAcceptThread = CreateThread(NULL, 0, AcceptThreadProc, NULL, 0, &dwThreadId);

    if (NULL == g_hAcceptThread)
    {
        InterlockedExchange(&g_lRunning, 0);
        SockStatus = SOCKET_STATUS_ERROR;
        goto EXIT_RELAY_START;
    }

    Notify(RELAY_NOTIFY_LISTENING, 0);
    SockStatus = SOCKET_STATUS_SUCCESS;

EXIT_RELAY_START:

    if (SOCKET_IS_ERROR(SockStatus) && bOwnsState)
    {
        RelayStop();
    }

    return SockStatus;

} /* RelayStart */

VOID RelayStop(VOID)
{
    if (NULL != g_hAbort)
    {
        (VOID) SetEvent(g_hAbort);
    }

    /* The accept thread joins the sessions it started, so waiting on it here
       is enough to know every worker is gone. */
    if (NULL != g_hAcceptThread)
    {
        (VOID) WaitForSingleObject(g_hAcceptThread, INFINITE);
        (VOID) CloseHandle(g_hAcceptThread);
        g_hAcceptThread = NULL;
    }
    else
    {
        SessionTableJoinAll();
    }

    if (NULL != g_pListener)
    {
        (VOID) SocketContextCleanup(&g_pListener);
        g_pListener = NULL;
    }

    if (NULL != g_hAbort)
    {
        (VOID) CloseHandle(g_hAbort);
        g_hAbort = NULL;
    }

    if (g_bSessionLockReady)
    {
        DeleteCriticalSection(&g_SessionLock);
        g_bSessionLockReady = FALSE;
    }

    SecureZeroMemory(&g_Config, sizeof(g_Config));
    g_pfnNotify      = NULL;
    g_pNotifyContext = NULL;
    InterlockedExchange(&g_lRunning, 0);

    return;

} /* RelayStop */

static VOID Notify(_In_ RELAY_NOTIFY Code, _In_ LPARAM lData)
{
    if (NULL == g_pfnNotify)
    {
        goto EXIT_NOTIFY;
    }

    /* Called directly, never posted or queued: Relay.c has no thread of its
       own to defer to, so the callback itself is what must not block. The
       GUI's callback only calls PostMessageW and the CLI's only prints. */
    g_pfnNotify(g_pNotifyContext, Code, lData);

EXIT_NOTIFY:

    return;

} /* Notify */

static DWORD WINAPI AcceptThreadProc(_In_ LPVOID lpParameter)
{
    SOCKET_STATUS  SockStatus = SOCKET_STATUS_ERROR;
    PRELAY_SESSION pSession   = NULL;
    HANDLE         hThread    = NULL;
    LONG           lSlot      = -1;
    DWORD          dwThreadId = 0;

    (VOID) lpParameter;

    for (;;)
    {
        if (WAIT_OBJECT_0 == WaitForSingleObject(g_hAbort, 0))
        {
            break;
        }

        pSession = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(RELAY_SESSION));

        if (NULL == pSession)
        {
            Notify(RELAY_NOTIFY_ERROR, (LPARAM)SOCKET_STATUS_BAD_MEMORY_ALLOC);
            break;
        }

        (VOID) SocketConnInit(&pSession->Client);
        (VOID) SocketConnInit(&pSession->Target);

        SockStatus = SocketAccept(g_pListener, RELAY_ACCEPT_MS, g_hAbort, &pSession->Client);

        if (SOCKET_STATUS_TIMEOUT == SockStatus)
        {
            /* Nothing arrived in this slice. Free the shell and look again. */
            (VOID) HeapFree(GetProcessHeap(), 0, pSession);
            pSession = NULL;
            continue;
        }

        if (SOCKET_STATUS_ABORTED == SockStatus)
        {
            (VOID) HeapFree(GetProcessHeap(), 0, pSession);
            pSession = NULL;
            break;
        }

        if (SOCKET_IS_ERROR(SockStatus))
        {
            (VOID) HeapFree(GetProcessHeap(), 0, pSession);
            pSession = NULL;
            Notify(RELAY_NOTIFY_ERROR, (LPARAM)SockStatus);
            break;
        }

        CopyMemory(&pSession->Config, &g_Config, sizeof(RELAY_CONFIG));

        /* Find the slot before spawning. Refusing a client we cannot track is
           better than starting a thread we would have to kill, and this is
           the only thread that ever claims a slot, so the choice holds. */
        lSlot = SessionTableClaim();

        if (0 > lSlot)
        {
            SocketConnClose(&pSession->Client);
            (VOID) HeapFree(GetProcessHeap(), 0, pSession);
            pSession = NULL;
            Notify(RELAY_NOTIFY_ERROR, (LPARAM)SOCKET_STATUS_ERROR);
            continue;
        }

        hThread = CreateThread(NULL, 0, SessionThreadProc, pSession, 0, &dwThreadId);

        if (NULL == hThread)
        {
            SessionTableStore(lSlot, NULL);
            SocketConnClose(&pSession->Client);
            (VOID) HeapFree(GetProcessHeap(), 0, pSession);
            pSession = NULL;
            Notify(RELAY_NOTIFY_ERROR, (LPARAM)SOCKET_STATUS_ERROR);
            continue;
        }

        /* A thread handle stays valid after the thread exits, so storing it
           late is safe even if the session has already finished. */
        SessionTableStore(lSlot, hThread);

        hThread  = NULL;
        pSession = NULL;
    }

    SessionTableJoinAll();
    Notify(RELAY_NOTIFY_STOPPED, 0);

    return 0;

} /* AcceptThreadProc */

static DWORD WINAPI SessionThreadProc(_In_ LPVOID lpParameter)
{
    SOCKET_STATUS  SockStatus = SOCKET_STATUS_ERROR;
    PRELAY_SESSION pSession   = (PRELAY_SESSION)lpParameter;

    if (NULL == pSession)
    {
        goto EXIT_SESSION_THREAD;
    }

    SockStatus = SocketConnConnect(&pSession->Target,
                                   pSession->Config.wszTargetAddr,
                                   pSession->Config.wszTargetPort,
                                   RELAY_CONNECT_MS,
                                   g_hAbort);

    if (SOCKET_IS_ERROR(SockStatus))
    {
        /* The client is already connected to us, so a dead target has to be
           reported and dropped rather than left hanging. */
        Notify(RELAY_NOTIFY_ERROR, (LPARAM)SockStatus);
        goto EXIT_SESSION_THREAD;
    }

    Notify(RELAY_NOTIFY_ACCEPTED, 0);

    SessionPump(pSession);

    Notify(RELAY_NOTIFY_CLOSED, 0);

EXIT_SESSION_THREAD:

    if (NULL != pSession)
    {
        SocketConnClose(&pSession->Target);
        SocketConnClose(&pSession->Client);
        SecureZeroMemory(pSession, sizeof(RELAY_SESSION));
        (VOID) HeapFree(GetProcessHeap(), 0, pSession);
        pSession = NULL;
    }

    return 0;

} /* SessionThreadProc */

static VOID SessionPump(_In_ PRELAY_SESSION pSession)
{
    RELAY_DIR ToTarget                = {0};
    RELAY_DIR ToClient                = {0};
    WSAEVENT  ahEvents[WAIT_SLOT_MAX] = {0};
    PBYTE     pbScratch               = NULL;
    LONG      lClientEvents           = 0;
    LONG      lTargetEvents           = 0;
    DWORD     dwWaitMs                = 0;
    DWORD     dwOtherMs               = 0;
    DWORD     dwWait                  = 0;

    if (NULL == pSession)
    {
        goto EXIT_PUMP;
    }

    pbScratch = HeapAlloc(GetProcessHeap(), 0, RELAY_CHUNK_CB);

    if (NULL == pbScratch)
    {
        Notify(RELAY_NOTIFY_ERROR, (LPARAM)SOCKET_STATUS_BAD_MEMORY_ALLOC);
        goto EXIT_PUMP;
    }

    /* One way. Only the direction that ends at the client is delayed, so a
       request/response exchange pays the latency once rather than twice.
       These two lines are the whole knob if that ever needs to change. */
    DirectionInit(&ToTarget, &pSession->Client, &pSession->Target, RELAY_NO_DELAY);
    DirectionInit(&ToClient, &pSession->Target, &pSession->Client, pSession->Config.dwLatencyMs);

    ahEvents[WAIT_SLOT_ABORT]  = g_hAbort;
    ahEvents[WAIT_SLOT_CLIENT] = pSession->Client.hEvent;
    ahEvents[WAIT_SLOT_TARGET] = pSession->Target.hEvent;

    for (;;)
    {
        /* Drain before waiting: the first read after connect has no event
           behind it, and a chunk queued last pass may already be due. */
        DirectionRead(&ToTarget, pbScratch, RELAY_CHUNK_CB);
        DirectionRead(&ToClient, pbScratch, RELAY_CHUNK_CB);

        DirectionWrite(&ToTarget);
        DirectionWrite(&ToClient);

        DirectionFinish(&ToTarget);
        DirectionFinish(&ToClient);

        if (DirectionIsDone(&ToTarget) && DirectionIsDone(&ToClient))
        {
            break;
        }

        /* A failure ends the session rather than leaving the healthy half
           waiting on a peer that will never answer. The drain below still
           delivers whatever that half had queued, so ending here loses
           no data. */
        if (ToTarget.bFailed || ToClient.bFailed)
        {
            break;
        }

        dwWaitMs  = DirectionWaitMs(&ToTarget);
        dwOtherMs = DirectionWaitMs(&ToClient);

        if (dwOtherMs < dwWaitMs)
        {
            dwWaitMs = dwOtherMs;
        }

        /* See RELAY_MAX_PUMP_WAIT_MS: the generic event poll below can
           consume a socket's pending FD_READ/FD_WRITE notification on a pass
           where back pressure or an unmet due time kept DirectionRead or
           DirectionWrite from ever making the call that was supposed to earn
           it, which otherwise stalls the session until the peer gives up.
           Applied unconditionally because either direction can be the one
           left holding a swallowed notification, not just whichever one
           happens to look stalled at this instant. */
        if (dwWaitMs > (DWORD)RELAY_MAX_PUMP_WAIT_MS)
        {
            dwWaitMs = (DWORD)RELAY_MAX_PUMP_WAIT_MS;
        }

        dwWait = WSAWaitForMultipleEvents(WAIT_SLOT_MAX, ahEvents, FALSE, dwWaitMs, FALSE);

        if (WSA_WAIT_FAILED == dwWait)
        {
            break;
        }

        if ((WSA_WAIT_EVENT_0 + WAIT_SLOT_ABORT) == dwWait)
        {
            break;
        }

        /* Whichever handle woke us, both sockets get polled with a zero wait.
           WSAEnumNetworkEvents clears as it reports, so the bits have to be
           collected in one pass and then handed to both directions. */
        (VOID) SocketConnWait(&pSession->Client, 0, NULL, &lClientEvents);
        (VOID) SocketConnWait(&pSession->Target, 0, NULL, &lTargetEvents);

        DirectionApplyEvents(&ToTarget, lClientEvents, lTargetEvents);
        DirectionApplyEvents(&ToClient, lTargetEvents, lClientEvents);
    }

    /* Anything still queued here is owed to a peer that is still listening,
       so it gets delivered rather than freed. An abort skips this on purpose:
       discarding in-flight bytes is the whole point of a stop button. */
    if (WAIT_OBJECT_0 != WaitForSingleObject(g_hAbort, 0))
    {
        DirectionDrain(&ToTarget, g_hAbort);
        DirectionDrain(&ToClient, g_hAbort);
    }

EXIT_PUMP:

    DirectionFree(&ToTarget);
    DirectionFree(&ToClient);

    if (NULL != pbScratch)
    {
        (VOID) HeapFree(GetProcessHeap(), 0, pbScratch);
        pbScratch = NULL;
    }

    return;

} /* SessionPump */

static LONG SessionTableClaim(VOID)
{
    LONG  lResult = -1;
    DWORD dwIndex = 0;

    if (!g_bSessionLockReady)
    {
        goto EXIT_TABLE_CLAIM;
    }

    EnterCriticalSection(&g_SessionLock);

    /* Reap first: finished sessions leave a signalled handle behind, and this
       is the only place that can free the slot without blocking. */
    for (dwIndex = 0; dwIndex < (DWORD)RELAY_MAX_SESSIONS; dwIndex++)
    {
        if ((NULL == g_ahSessions[dwIndex]) || (INVALID_HANDLE_VALUE == g_ahSessions[dwIndex]))
        {
            continue;
        }

        if (WAIT_OBJECT_0 == WaitForSingleObject(g_ahSessions[dwIndex], 0))
        {
            (VOID) CloseHandle(g_ahSessions[dwIndex]);
            g_ahSessions[dwIndex] = NULL;
        }
    }

    /* INVALID_HANDLE_VALUE marks the slot as spoken for. The join skips it,
       which matters only in the sliver of time before the handle lands. */
    for (dwIndex = 0; dwIndex < (DWORD)RELAY_MAX_SESSIONS; dwIndex++)
    {
        if (NULL == g_ahSessions[dwIndex])
        {
            g_ahSessions[dwIndex] = INVALID_HANDLE_VALUE;
            lResult               = (LONG)dwIndex;
            break;
        }
    }

    LeaveCriticalSection(&g_SessionLock);

EXIT_TABLE_CLAIM:

    return lResult;

} /* SessionTableClaim */

static VOID SessionTableStore(_In_ LONG lIndex, _In_opt_ HANDLE hThread)
{
    if ((!g_bSessionLockReady) || (0 > lIndex) || (RELAY_MAX_SESSIONS <= lIndex))
    {
        goto EXIT_TABLE_STORE;
    }

    EnterCriticalSection(&g_SessionLock);

    g_ahSessions[lIndex] = hThread;

    LeaveCriticalSection(&g_SessionLock);

EXIT_TABLE_STORE:

    return;

} /* SessionTableStore */

static VOID SessionTableJoinAll(VOID)
{
    HANDLE ahLocal[RELAY_MAX_SESSIONS] = {0};
    DWORD  dwIndex                     = 0;

    if (!g_bSessionLockReady)
    {
        goto EXIT_JOIN_ALL;
    }

    /* Copy under the lock, wait outside it. Holding the lock across a wait
       would block the accept thread that is trying to hand back a slot. */
    EnterCriticalSection(&g_SessionLock);

    CopyMemory(ahLocal, g_ahSessions, sizeof(ahLocal));
    SecureZeroMemory(g_ahSessions, sizeof(g_ahSessions));

    LeaveCriticalSection(&g_SessionLock);

    for (dwIndex = 0; dwIndex < (DWORD)RELAY_MAX_SESSIONS; dwIndex++)
    {
        if ((NULL == ahLocal[dwIndex]) || (INVALID_HANDLE_VALUE == ahLocal[dwIndex]))
        {
            continue;
        }

        (VOID) WaitForSingleObject(ahLocal[dwIndex], INFINITE);
        (VOID) CloseHandle(ahLocal[dwIndex]);
        ahLocal[dwIndex] = NULL;
    }

EXIT_JOIN_ALL:

    return;

} /* SessionTableJoinAll */

static VOID DirectionInit(_Out_ PRELAY_DIR  pDir,
                          _In_ PSOCKET_CONN pSource,
                          _In_ PSOCKET_CONN pSink,
                          _In_ DWORD        dwLatencyMs)
{
    if (NULL == pDir)
    {
        goto EXIT_DIR_INIT;
    }

    SecureZeroMemory(pDir, sizeof(RELAY_DIR));

    pDir->pSource     = pSource;
    pDir->pSink       = pSink;
    pDir->dwLatencyMs = dwLatencyMs;

    /* Optimistic. A socket fresh from accept or connect is usually both
       readable and writable, and a WSAEWOULDBLOCK corrects the guess at no
       cost beyond one syscall. */
    pDir->bSourceReadable = TRUE;
    pDir->bSinkWritable   = TRUE;

EXIT_DIR_INIT:

    return;

} /* DirectionInit */

static VOID DirectionFree(_Inout_opt_ PRELAY_DIR pDir)
{
    PDELAY_CHUNK pChunk = NULL;
    PDELAY_CHUNK pNext  = NULL;

    if (NULL == pDir)
    {
        goto EXIT_DIR_FREE;
    }

    pChunk = pDir->pHead;

    while (NULL != pChunk)
    {
        pNext = pChunk->pNext;
        (VOID) HeapFree(GetProcessHeap(), 0, pChunk);
        pChunk = pNext;
    }

    pDir->pHead    = NULL;
    pDir->pTail    = NULL;
    pDir->cbQueued = 0;

EXIT_DIR_FREE:

    return;

} /* DirectionFree */

static VOID
DirectionApplyEvents(_Inout_opt_ PRELAY_DIR pDir, _In_ LONG lSourceEvents, _In_ LONG lSinkEvents)
{
    if (NULL == pDir)
    {
        goto EXIT_APPLY_EVENTS;
    }

    /* FD_CLOSE means there may still be buffered data to read, so it re-arms
       the reader rather than ending the direction on the spot. */
    if (0 != (lSourceEvents & (FD_READ | FD_CLOSE)))
    {
        pDir->bSourceReadable = TRUE;
    }

    if (0 != (lSinkEvents & FD_WRITE))
    {
        pDir->bSinkWritable = TRUE;
    }

EXIT_APPLY_EVENTS:

    return;

} /* DirectionApplyEvents */

static VOID DirectionRead(_Inout_opt_ PRELAY_DIR                  pDir,
                          _Out_writes_bytes_opt_(cbScratch) PBYTE pbScratch,
                          _In_ DWORD                              cbScratch)
{
    SOCKET_IO    IoResult = SOCKET_IO_ERROR;
    PDELAY_CHUNK pChunk   = NULL;
    DWORD        cbRead   = 0;

    if ((NULL == pDir) || (NULL == pbScratch) || (0 == cbScratch))
    {
        goto EXIT_DIR_READ;
    }

    while (pDir->bSourceReadable && (!pDir->bSourceClosed) && (!pDir->bFailed))
    {
        /* Back pressure. Without this a fast sender plus a long delay grows
           the queue without bound; stopping the reads lets the sender's own
           window do the throttling for us. */
        if (pDir->cbQueued >= (DWORD)RELAY_QUEUE_MAX_CB)
        {
            break;
        }

        IoResult = SocketConnRecv(pDir->pSource, pbScratch, cbScratch, &cbRead);

        if (SOCKET_IO_WOULD_BLOCK == IoResult)
        {
            pDir->bSourceReadable = FALSE;
            break;
        }

        if (SOCKET_IO_CLOSED == IoResult)
        {
            pDir->bSourceClosed = TRUE;
            break;
        }

        if (SOCKET_IO_OK != IoResult)
        {
            pDir->bFailed = TRUE;
            break;
        }

        pChunk = HeapAlloc(GetProcessHeap(), 0, CHUNK_HEADER_CB + cbRead);

        if (NULL == pChunk)
        {
            pDir->bFailed = TRUE;
            break;
        }

        pChunk->pNext      = NULL;
        pChunk->cbData     = cbRead;
        pChunk->cbSent     = 0;
        pChunk->ullDueTick = GetTickCount64() + pDir->dwLatencyMs;

        CopyMemory(pChunk->abData, pbScratch, cbRead);

        if (NULL == pDir->pTail)
        {
            pDir->pHead = pChunk;
            pDir->pTail = pChunk;
        }
        else
        {
            pDir->pTail->pNext = pChunk;
            pDir->pTail        = pChunk;
        }

        pDir->cbQueued += cbRead;
        pChunk = NULL;
    }

EXIT_DIR_READ:

    return;

} /* DirectionRead */

static VOID DirectionWrite(_Inout_opt_ PRELAY_DIR pDir)
{
    SOCKET_IO    IoResult = SOCKET_IO_ERROR;
    PDELAY_CHUNK pChunk   = NULL;
    ULONGLONG    ullNow   = 0;
    DWORD        cbSent   = 0;

    if (NULL == pDir)
    {
        goto EXIT_DIR_WRITE;
    }

    ullNow = GetTickCount64();

    while (pDir->bSinkWritable && (NULL != pDir->pHead) && (!pDir->bFailed))
    {
        pChunk = pDir->pHead;

        /* The queue is in arrival order and every chunk gets the same delay,
           so the head is always the earliest due. One comparison is enough. */
        if (ullNow < pChunk->ullDueTick)
        {
            break;
        }

        IoResult = SocketConnSend(pDir->pSink,
                                  pChunk->abData + pChunk->cbSent,
                                  pChunk->cbData - pChunk->cbSent,
                                  &cbSent);

        if (SOCKET_IO_WOULD_BLOCK == IoResult)
        {
            pDir->bSinkWritable = FALSE;
            break;
        }

        if (SOCKET_IO_OK != IoResult)
        {
            pDir->bFailed = TRUE;
            break;
        }

        /* A successful send of nothing would spin this loop forever, so it is
           treated as back pressure. */
        if (0 == cbSent)
        {
            pDir->bSinkWritable = FALSE;
            break;
        }

        pChunk->cbSent += cbSent;
        pDir->cbQueued -= cbSent;

        if (pChunk->cbSent < pChunk->cbData)
        {
            continue;
        }

        pDir->pHead = pChunk->pNext;

        if (NULL == pDir->pHead)
        {
            pDir->pTail = NULL;
        }

        (VOID) HeapFree(GetProcessHeap(), 0, pChunk);
        pChunk = NULL;
    }

EXIT_DIR_WRITE:

    return;

} /* DirectionWrite */

static VOID DirectionFinish(_Inout_opt_ PRELAY_DIR pDir)
{
    if (NULL == pDir)
    {
        goto EXIT_DIR_FINISH;
    }

    if (pDir->bFailed || pDir->bSinkShutdown || (!pDir->bSourceClosed) || (NULL != pDir->pHead))
    {
        goto EXIT_DIR_FINISH;
    }

    /* Half close only. The other direction may still have queued bytes owed
       to this socket, and closing outright would discard them. */
    (VOID) SocketConnShutdownSend(pDir->pSink);
    pDir->bSinkShutdown = TRUE;

EXIT_DIR_FINISH:

    return;

} /* DirectionFinish */

static VOID DirectionDrain(_Inout_opt_ PRELAY_DIR pDir, _In_opt_ HANDLE hAbort)
{
    ULONGLONG ullDeadline = 0;
    LONG      lEvents     = 0;
    DWORD     dwWaitMs    = 0;

    if (NULL == pDir)
    {
        goto EXIT_DRAIN;
    }

    ullDeadline = GetTickCount64() + RELAY_DRAIN_MS;

    while ((NULL != pDir->pHead) && (!pDir->bFailed))
    {
        DirectionWrite(pDir);

        if ((NULL == pDir->pHead) || pDir->bFailed)
        {
            break;
        }

        if (GetTickCount64() >= ullDeadline)
        {
            break;
        }

        /* Bounded on purpose. DirectionWaitMs reports INFINITE when only an
           FD_WRITE can help, and trusting that here is how the drain turns
           into a hang if the sink goes quiet. */
        dwWaitMs = DirectionWaitMs(pDir);

        if ((INFINITE == dwWaitMs) || (RELAY_DRAIN_SLICE < dwWaitMs))
        {
            dwWaitMs = RELAY_DRAIN_SLICE;
        }

        (VOID) SocketConnWait(pDir->pSink, dwWaitMs, hAbort, &lEvents);

        if ((NULL != hAbort) && (WAIT_OBJECT_0 == WaitForSingleObject(hAbort, 0)))
        {
            break;
        }

        /* Retry the send regardless of what fired. A wasted attempt costs one
           syscall and sets the flag back to FALSE on WSAEWOULDBLOCK. */
        pDir->bSinkWritable = TRUE;
    }

    DirectionFinish(pDir);

EXIT_DRAIN:

    return;

} /* DirectionDrain */

static BOOL DirectionIsDone(_In_opt_ CONST RELAY_DIR *pDir)
{
    BOOL bResult = TRUE;

    if (NULL == pDir)
    {
        goto EXIT_DIR_DONE;
    }

    bResult = (pDir->bFailed || pDir->bSinkShutdown);

EXIT_DIR_DONE:

    return bResult;

} /* DirectionIsDone */

static DWORD DirectionWaitMs(_In_opt_ CONST RELAY_DIR *pDir)
{
    DWORD     dwWaitMs = INFINITE;
    ULONGLONG ullNow   = 0;

    if ((NULL == pDir) || (NULL == pDir->pHead))
    {
        goto EXIT_DIR_WAIT;
    }

    /* Nothing can be sent until FD_WRITE arrives, and that is an event, not a
       deadline. Asking for a timer here would just spin. */
    if (!pDir->bSinkWritable)
    {
        goto EXIT_DIR_WAIT;
    }

    ullNow = GetTickCount64();

    if (pDir->pHead->ullDueTick <= ullNow)
    {
        dwWaitMs = 0;
    }
    else
    {
        dwWaitMs = (DWORD)(pDir->pHead->ullDueTick - ullNow);
    }

EXIT_DIR_WAIT:

    return dwWaitMs;

} /* DirectionWaitMs */

/* end of file */
