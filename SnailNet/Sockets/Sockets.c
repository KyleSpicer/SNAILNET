/**
 * @file Sockets.c
 * @brief Event-driven Winsock wrapper for TCP listeners and connections.
 */

#include <WinSock2.h>
#include <WS2tcpip.h>
#include <Windows.h>

#include "Sockets.h"

/**
 * @brief Resolves an address and port into an addrinfo list.
 *
 * @param pwszAddress Address text.
 * @param pwszPort    Port text.
 * @param bPassive    TRUE for a bind address, FALSE for a connect target.
 * @param ppaiResults Receives the list. Caller frees with FreeAddrInfoW.
 *
 * @return SOCKET_STATUS_SUCCESS on success, error code on failure.
 */
static SOCKET_STATUS ResolveHostAddress(_In_z_ PCWSTR        pwszAddress,
                                        _In_z_ PCWSTR        pwszPort,
                                        _In_ BOOL            bPassive,
                                        _Outptr_ PADDRINFOW *ppaiResults);

/**
 * @brief Waits on a network event plus an optional abort handle.
 *
 * @param hNetworkEvent Event to wait on.
 * @param dwTimeoutMs   Wait limit, or INFINITE.
 * @param hAbort        Optional cancel event.
 *
 * @return SOCKET_STATUS_SUCCESS if the network event fired,
 *         SOCKET_STATUS_TIMEOUT, SOCKET_STATUS_ABORTED, or an error.
 */
static SOCKET_STATUS
WaitForNetworkOrAbort(_In_ WSAEVENT hNetworkEvent, _In_ DWORD dwTimeoutMs, _In_opt_ HANDLE hAbort);

/**
 * @brief Creates a socket and binds it to an event with the given mask.
 *
 * @param paiEntry Address entry describing the family and protocol.
 * @param lEvents  FD_* mask to select.
 * @param pSocket  Receives the socket.
 * @param phEvent  Receives the event.
 *
 * @return SOCKET_STATUS_SUCCESS on success, error code on failure.
 */
static SOCKET_STATUS CreateEventSocket(_In_ PADDRINFOW paiEntry,
                                       _In_ LONG       lEvents,
                                       _Out_ SOCKET   *pSocket,
                                       _Out_ WSAEVENT *phEvent);

SOCKET_STATUS
SocketLibInit(VOID)
{
    SOCKET_STATUS SockStatus = SOCKET_STATUS_ERROR;
    WSADATA       WsaData    = {0};
    INT           iResult    = 0;

    iResult =
        WSAStartup(MAKEWORD(SOCKET_WINSOCK_VERSION_MAJOR, SOCKET_WINSOCK_VERSION_MINOR), &WsaData);

    if (0 != iResult)
    {
        SockStatus = SOCKET_STATUS_WSA_STARTUP_FAIL;
        goto EXIT_LIB_INIT;
    }

    SockStatus = SOCKET_STATUS_SUCCESS;

EXIT_LIB_INIT:

    return SockStatus;

} /* SocketLibInit */

VOID SocketLibCleanup(VOID) { (VOID) WSACleanup(); } /* SocketLibCleanup */

SOCKET_STATUS
SocketResolveCheck(_In_z_ PCWSTR pwszAddress, _In_z_ PCWSTR pwszPort)
{
    SOCKET_STATUS SockStatus = SOCKET_STATUS_ERROR;
    PADDRINFOW    paiResults = NULL;

    if ((NULL == pwszAddress) || (NULL == pwszPort))
    {
        SockStatus = SOCKET_STATUS_INVALID_ARGS;
        goto EXIT_RESOLVE_CHECK;
    }

    /* FALSE: this is a connect target, so AI_PASSIVE must stay off or an
       empty address would quietly resolve to a wildcard. */
    SockStatus = ResolveHostAddress(pwszAddress, pwszPort, FALSE, &paiResults);

EXIT_RESOLVE_CHECK:

    if (NULL != paiResults)
    {
        FreeAddrInfoW(paiResults);
        paiResults = NULL;
    }

    return SockStatus;

} /* SocketResolveCheck */

SOCKET_STATUS
SocketContextCreate(_Inout_ PSOCKET_CTX *ppSocketCtx)
{
    SOCKET_STATUS SockStatus = SOCKET_STATUS_ERROR;
    PSOCKET_CTX   pSocketCtx = NULL;

    if ((NULL == ppSocketCtx) || (NULL != *ppSocketCtx))
    {
        SockStatus = SOCKET_STATUS_INVALID_ARGS;
        goto EXIT_CTX_CREATE;
    }

    pSocketCtx = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(SOCKET_CTX));

    if (NULL == pSocketCtx)
    {
        SockStatus = SOCKET_STATUS_BAD_MEMORY_ALLOC;
        goto EXIT_CTX_CREATE;
    }

    pSocketCtx->Socket        = INVALID_SOCKET;
    pSocketCtx->hNetworkEvent = WSA_INVALID_EVENT;
    pSocketCtx->paiResults    = NULL;

    *ppSocketCtx = pSocketCtx;
    pSocketCtx   = NULL;
    SockStatus   = SOCKET_STATUS_SUCCESS;

EXIT_CTX_CREATE:

    if (NULL != pSocketCtx)
    {
        (VOID) HeapFree(GetProcessHeap(), 0, pSocketCtx);
        pSocketCtx = NULL;
    }

    return SockStatus;

} /* SocketContextCreate */

SOCKET_STATUS
SocketContextInit(_Inout_ PSOCKET_CTX pSocketCtx, _In_z_ PCWSTR pwszAddress, _In_z_ PCWSTR pwszPort)
{
    SOCKET_STATUS SockStatus = SOCKET_STATUS_ERROR;

    if ((NULL == pSocketCtx) || (NULL == pwszAddress) || (NULL == pwszPort))
    {
        SockStatus = SOCKET_STATUS_INVALID_ARGS;
        goto EXIT_CTX_INIT;
    }

    /* Re-initialising an already initialised context would strand the old
       address list and socket, so refuse rather than leak. */
    if ((NULL != pSocketCtx->paiResults) || (INVALID_SOCKET != pSocketCtx->Socket))
    {
        SockStatus = SOCKET_STATUS_INVALID_ARGS;
        goto EXIT_CTX_INIT;
    }

    SockStatus = ResolveHostAddress(pwszAddress, pwszPort, TRUE, &pSocketCtx->paiResults);

    if (SOCKET_IS_ERROR(SockStatus))
    {
        goto EXIT_CTX_INIT;
    }

    SockStatus = CreateEventSocket(pSocketCtx->paiResults,
                                   FD_ACCEPT,
                                   &pSocketCtx->Socket,
                                   &pSocketCtx->hNetworkEvent);

EXIT_CTX_INIT:

    return SockStatus;

} /* SocketContextInit */

SOCKET_STATUS
SocketContextCleanup(_Inout_ PSOCKET_CTX *ppSocketCtx)
{
    SOCKET_STATUS SockStatus = SOCKET_STATUS_ERROR;
    PSOCKET_CTX   pSocketCtx = NULL;

    if (NULL == ppSocketCtx)
    {
        SockStatus = SOCKET_STATUS_INVALID_ARGS;
        goto EXIT_CTX_CLEANUP;
    }

    /* Idempotent: cleaning up an already cleaned context is a no-op, not an
       argument error. Callers release contexts from their own Exit blocks
       without knowing how far initialisation got. */
    if (NULL == *ppSocketCtx)
    {
        SockStatus = SOCKET_STATUS_SUCCESS;
        goto EXIT_CTX_CLEANUP;
    }

    pSocketCtx = *ppSocketCtx;

    if (INVALID_SOCKET != pSocketCtx->Socket)
    {
        (VOID) closesocket(pSocketCtx->Socket);
        pSocketCtx->Socket = INVALID_SOCKET;
    }

    if (WSA_INVALID_EVENT != pSocketCtx->hNetworkEvent)
    {
        (VOID) WSACloseEvent(pSocketCtx->hNetworkEvent);
        pSocketCtx->hNetworkEvent = WSA_INVALID_EVENT;
    }

    /* FreeAddrInfoW walks ai_next, so the list must not be overwritten before
       it runs. Zeroing it first strands every node after the first. */
    if (NULL != pSocketCtx->paiResults)
    {
        FreeAddrInfoW(pSocketCtx->paiResults);
        pSocketCtx->paiResults = NULL;
    }

    SecureZeroMemory(pSocketCtx, sizeof(SOCKET_CTX));
    (VOID) HeapFree(GetProcessHeap(), 0, pSocketCtx);
    pSocketCtx = NULL;

    *ppSocketCtx = NULL;
    SockStatus   = SOCKET_STATUS_SUCCESS;

EXIT_CTX_CLEANUP:

    return SockStatus;

} /* SocketContextCleanup */

SOCKET_STATUS
SocketBindAndListen(_In_ PSOCKET_CTX pSocketCtx)
{
    SOCKET_STATUS SockStatus = SOCKET_STATUS_ERROR;
    INT           iResult    = 0;

    if ((NULL == pSocketCtx) || (NULL == pSocketCtx->paiResults) ||
        (INVALID_SOCKET == pSocketCtx->Socket))
    {
        SockStatus = SOCKET_STATUS_INVALID_ARGS;
        goto EXIT_BIND_LISTEN;
    }

    iResult = bind(pSocketCtx->Socket,
                   pSocketCtx->paiResults->ai_addr,
                   (INT)pSocketCtx->paiResults->ai_addrlen);

    if (SOCKET_ERROR == iResult)
    {
        SockStatus = SOCKET_STATUS_BIND_ERROR;
        goto EXIT_BIND_LISTEN;
    }

    iResult = listen(pSocketCtx->Socket, SOCKET_LISTEN_BACKLOG);

    if (SOCKET_ERROR == iResult)
    {
        SockStatus = SOCKET_STATUS_LISTEN_ERROR;
        goto EXIT_BIND_LISTEN;
    }

    SockStatus = SOCKET_STATUS_SUCCESS;

EXIT_BIND_LISTEN:

    return SockStatus;

} /* SocketBindAndListen */

SOCKET_STATUS
SocketAccept(_In_ PSOCKET_CTX   pSocketCtx,
             _In_ DWORD         dwTimeoutMs,
             _In_opt_ HANDLE    hAbort,
             _Out_ PSOCKET_CONN pConn)
{
    SOCKET_STATUS    SockStatus = SOCKET_STATUS_ERROR;
    WSANETWORKEVENTS NetEvents  = {0};
    SOCKADDR_STORAGE PeerAddr   = {0};
    SOCKET           sAccepted  = INVALID_SOCKET;
    WSAEVENT         hEvent     = WSA_INVALID_EVENT;
    INT              iPeerLen   = (INT)sizeof(PeerAddr);
    INT              iResult    = 0;

    if (NULL != pConn)
    {
        (VOID) SocketConnInit(pConn);
    }

    if ((NULL == pSocketCtx) || (NULL == pConn) || (INVALID_SOCKET == pSocketCtx->Socket) ||
        (WSA_INVALID_EVENT == pSocketCtx->hNetworkEvent))
    {
        SockStatus = SOCKET_STATUS_INVALID_ARGS;
        goto EXIT_ACCEPT;
    }

    SockStatus = WaitForNetworkOrAbort(pSocketCtx->hNetworkEvent, dwTimeoutMs, hAbort);

    if (SOCKET_IS_ERROR(SockStatus))
    {
        goto EXIT_ACCEPT;
    }

    /* Enumerating resets the event and clears the recorded bits, so this is
       the only chance to see them. */
    iResult = WSAEnumNetworkEvents(pSocketCtx->Socket, pSocketCtx->hNetworkEvent, &NetEvents);

    if (SOCKET_ERROR == iResult)
    {
        SockStatus = SOCKET_STATUS_ACCEPT_ERROR;
        goto EXIT_ACCEPT;
    }

    if (0 == (NetEvents.lNetworkEvents & FD_ACCEPT))
    {
        SockStatus = SOCKET_STATUS_TIMEOUT;
        goto EXIT_ACCEPT;
    }

    if (0 != NetEvents.iErrorCode[FD_ACCEPT_BIT])
    {
        SockStatus = SOCKET_STATUS_ACCEPT_ERROR;
        goto EXIT_ACCEPT;
    }

    /* Locals, not the listener's own resolved address: WSAAccept writes the
       peer address into whatever buffer it is given. */
    sAccepted =
        WSAAccept(pSocketCtx->Socket, (PSOCKADDR)&PeerAddr, &iPeerLen, NULL, (DWORD_PTR)NULL);

    if (INVALID_SOCKET == sAccepted)
    {
        SockStatus = SOCKET_STATUS_ACCEPT_ERROR;
        goto EXIT_ACCEPT;
    }

    hEvent = WSACreateEvent();

    if (WSA_INVALID_EVENT == hEvent)
    {
        SockStatus = SOCKET_STATUS_CREATE_EVENT_FAIL;
        goto EXIT_ACCEPT;
    }

    /* An accepted socket inherits the listener's event and FD_ACCEPT mask.
       Selecting here replaces both, which is what makes the connection
       report FD_READ and FD_CLOSE rather than nothing at all. */
    iResult = WSAEventSelect(sAccepted, hEvent, SOCKET_CONN_EVENTS);

    if (SOCKET_ERROR == iResult)
    {
        SockStatus = SOCKET_STATUS_EVENT_SELECT_FAIL;
        goto EXIT_ACCEPT;
    }

    /* WSAAccept can shorten iPeerLen but never lengthens it past what it was
       given, so this is a guard against a hostile provider, not the stack. */
    if ((0 > iPeerLen) || ((INT)sizeof(PeerAddr) < iPeerLen))
    {
        iPeerLen = (INT)sizeof(PeerAddr);
    }

    pConn->Socket   = sAccepted;
    pConn->hEvent   = hEvent;
    pConn->iPeerLen = iPeerLen;
    CopyMemory(&pConn->PeerAddr, &PeerAddr, sizeof(PeerAddr));

    sAccepted  = INVALID_SOCKET;
    hEvent     = WSA_INVALID_EVENT;
    SockStatus = SOCKET_STATUS_SUCCESS;

EXIT_ACCEPT:

    /* Only reached with live handles when a later step failed. */
    if (WSA_INVALID_EVENT != hEvent)
    {
        (VOID) WSACloseEvent(hEvent);
        hEvent = WSA_INVALID_EVENT;
    }

    if (INVALID_SOCKET != sAccepted)
    {
        (VOID) closesocket(sAccepted);
        sAccepted = INVALID_SOCKET;
    }

    return SockStatus;

} /* SocketAccept */

SOCKET_STATUS
SocketConnInit(_Out_ PSOCKET_CONN pConn)
{
    SOCKET_STATUS SockStatus = SOCKET_STATUS_ERROR;

    if (NULL == pConn)
    {
        SockStatus = SOCKET_STATUS_INVALID_ARGS;
        goto EXIT_CONN_INIT;
    }

    SecureZeroMemory(pConn, sizeof(SOCKET_CONN));

    pConn->Socket = INVALID_SOCKET;
    pConn->hEvent = WSA_INVALID_EVENT;

    SockStatus = SOCKET_STATUS_SUCCESS;

EXIT_CONN_INIT:

    return SockStatus;

} /* SocketConnInit */

SOCKET_STATUS
SocketConnConnect(_Inout_ PSOCKET_CONN pConn,
                  _In_z_ PCWSTR        pwszAddress,
                  _In_z_ PCWSTR        pwszPort,
                  _In_ DWORD           dwTimeoutMs,
                  _In_opt_ HANDLE      hAbort)
{
    SOCKET_STATUS    SockStatus = SOCKET_STATUS_ERROR;
    WSANETWORKEVENTS NetEvents  = {0};
    PADDRINFOW       paiResults = NULL;
    SOCKET           sSocket    = INVALID_SOCKET;
    WSAEVENT         hEvent     = WSA_INVALID_EVENT;
    SIZE_T           cbPeer     = 0;
    INT              iResult    = 0;

    if ((NULL == pConn) || (NULL == pwszAddress) || (NULL == pwszPort))
    {
        SockStatus = SOCKET_STATUS_INVALID_ARGS;
        goto EXIT_CONNECT;
    }

    (VOID) SocketConnInit(pConn);

    SockStatus = ResolveHostAddress(pwszAddress, pwszPort, FALSE, &paiResults);

    if (SOCKET_IS_ERROR(SockStatus))
    {
        goto EXIT_CONNECT;
    }

    /* FD_CONNECT is in the mask only until the handshake completes; it is
       replaced below so the caller never has to filter it out again. */
    SockStatus = CreateEventSocket(paiResults, FD_CONNECT | SOCKET_CONN_EVENTS, &sSocket, &hEvent);

    if (SOCKET_IS_ERROR(SockStatus))
    {
        goto EXIT_CONNECT;
    }

    iResult = connect(sSocket, paiResults->ai_addr, (INT)paiResults->ai_addrlen);

    /* WSAEventSelect made the socket non-blocking, so an immediate
       WSAEWOULDBLOCK is the expected result, not a failure. */
    if (SOCKET_ERROR == iResult)
    {
        if (WSAEWOULDBLOCK != WSAGetLastError())
        {
            SockStatus = SOCKET_STATUS_CONNECT_ERROR;
            goto EXIT_CONNECT;
        }

        SockStatus = WaitForNetworkOrAbort(hEvent, dwTimeoutMs, hAbort);

        if (SOCKET_IS_ERROR(SockStatus))
        {
            goto EXIT_CONNECT;
        }

        iResult = WSAEnumNetworkEvents(sSocket, hEvent, &NetEvents);

        if (SOCKET_ERROR == iResult)
        {
            SockStatus = SOCKET_STATUS_CONNECT_ERROR;
            goto EXIT_CONNECT;
        }

        if (0 == (NetEvents.lNetworkEvents & FD_CONNECT))
        {
            SockStatus = SOCKET_STATUS_TIMEOUT;
            goto EXIT_CONNECT;
        }

        /* A refused or unreachable target surfaces here, not from connect. */
        if (0 != NetEvents.iErrorCode[FD_CONNECT_BIT])
        {
            SockStatus = SOCKET_STATUS_CONNECT_ERROR;
            goto EXIT_CONNECT;
        }
    }

    iResult = WSAEventSelect(sSocket, hEvent, SOCKET_CONN_EVENTS);

    if (SOCKET_ERROR == iResult)
    {
        SockStatus = SOCKET_STATUS_EVENT_SELECT_FAIL;
        goto EXIT_CONNECT;
    }

    /* ai_addrlen comes from the resolver rather than from us, so clamp it
       before it becomes a length argument to CopyMemory. */
    cbPeer = (SIZE_T)paiResults->ai_addrlen;

    if (sizeof(pConn->PeerAddr) < cbPeer)
    {
        cbPeer = sizeof(pConn->PeerAddr);
    }

    pConn->Socket   = sSocket;
    pConn->hEvent   = hEvent;
    pConn->iPeerLen = (INT)cbPeer;
    CopyMemory(&pConn->PeerAddr, paiResults->ai_addr, cbPeer);

    sSocket    = INVALID_SOCKET;
    hEvent     = WSA_INVALID_EVENT;
    SockStatus = SOCKET_STATUS_SUCCESS;

EXIT_CONNECT:

    if (WSA_INVALID_EVENT != hEvent)
    {
        (VOID) WSACloseEvent(hEvent);
        hEvent = WSA_INVALID_EVENT;
    }

    if (INVALID_SOCKET != sSocket)
    {
        (VOID) closesocket(sSocket);
        sSocket = INVALID_SOCKET;
    }

    if (NULL != paiResults)
    {
        FreeAddrInfoW(paiResults);
        paiResults = NULL;
    }

    return SockStatus;

} /* SocketConnConnect */

SOCKET_STATUS
SocketConnWait(_In_ PSOCKET_CONN pConn,
               _In_ DWORD        dwTimeoutMs,
               _In_opt_ HANDLE   hAbort,
               _Out_ PLONG       plEvents)
{
    SOCKET_STATUS    SockStatus = SOCKET_STATUS_ERROR;
    WSANETWORKEVENTS NetEvents  = {0};
    INT              iResult    = 0;

    if (NULL != plEvents)
    {
        *plEvents = 0;
    }

    if ((NULL == pConn) || (NULL == plEvents) || (INVALID_SOCKET == pConn->Socket) ||
        (WSA_INVALID_EVENT == pConn->hEvent))
    {
        SockStatus = SOCKET_STATUS_INVALID_ARGS;
        goto EXIT_CONN_WAIT;
    }

    SockStatus = WaitForNetworkOrAbort(pConn->hEvent, dwTimeoutMs, hAbort);

    if (SOCKET_IS_ERROR(SockStatus))
    {
        goto EXIT_CONN_WAIT;
    }

    iResult = WSAEnumNetworkEvents(pConn->Socket, pConn->hEvent, &NetEvents);

    if (SOCKET_ERROR == iResult)
    {
        SockStatus = SOCKET_STATUS_ERROR;
        goto EXIT_CONN_WAIT;
    }

    *plEvents  = NetEvents.lNetworkEvents;
    SockStatus = SOCKET_STATUS_SUCCESS;

EXIT_CONN_WAIT:

    return SockStatus;

} /* SocketConnWait */

SOCKET_IO
SocketConnRecv(_In_ PSOCKET_CONN                  pConn,
               _Out_writes_bytes_(cbBuffer) PBYTE pbBuffer,
               _In_ DWORD                         cbBuffer,
               _Out_ PDWORD                       pcbRead)
{
    SOCKET_IO IoResult = SOCKET_IO_ERROR;
    WSABUF    WsaBuf   = {0};
    DWORD     dwRead   = 0;
    DWORD     dwFlags  = 0;
    INT       iResult  = 0;

    if (NULL != pcbRead)
    {
        *pcbRead = 0;
    }

    if ((NULL == pConn) || (NULL == pbBuffer) || (0 == cbBuffer) || (NULL == pcbRead) ||
        (INVALID_SOCKET == pConn->Socket))
    {
        goto EXIT_RECV;
    }

    WsaBuf.buf = (PCHAR)pbBuffer;
    WsaBuf.len = cbBuffer;

    iResult = WSARecv(pConn->Socket, &WsaBuf, 1, &dwRead, &dwFlags, NULL, NULL);

    if (0 == iResult)
    {
        /* Zero bytes on a successful call is an orderly FIN, not an error.
           Reporting it as one is how a proxy silently truncates transfers. */
        if (0 == dwRead)
        {
            IoResult = SOCKET_IO_CLOSED;
            goto EXIT_RECV;
        }

        *pcbRead = dwRead;
        IoResult = SOCKET_IO_OK;
        goto EXIT_RECV;
    }

    switch (WSAGetLastError())
    {
    case WSAEWOULDBLOCK:
        IoResult = SOCKET_IO_WOULD_BLOCK;
        break;

    /* WSAESHUTDOWN is this side having already shut the direction down, which
       is orderly. A reset or abort is not: the peer tore the connection down
       with data possibly still in flight, and calling that a clean EOF is how
       a relay truncates a transfer and reports success. */
    case WSAESHUTDOWN:
        IoResult = SOCKET_IO_CLOSED;
        break;

    case WSAECONNRESET:
    case WSAECONNABORTED:
        IoResult = SOCKET_IO_ERROR;
        break;

    default:
        IoResult = SOCKET_IO_ERROR;
        break;
    }

EXIT_RECV:

    return IoResult;

} /* SocketConnRecv */

SOCKET_IO
SocketConnSend(_In_ PSOCKET_CONN                  pConn,
               _In_reads_bytes_(cbBuffer) LPCBYTE pbBuffer,
               _In_ DWORD                         cbBuffer,
               _Out_ PDWORD                       pcbSent)
{
    SOCKET_IO IoResult = SOCKET_IO_ERROR;
    WSABUF    WsaBuf   = {0};
    DWORD     dwSent   = 0;
    INT       iResult  = 0;

    if (NULL != pcbSent)
    {
        *pcbSent = 0;
    }

    if ((NULL == pConn) || (NULL == pbBuffer) || (0 == cbBuffer) || (NULL == pcbSent) ||
        (INVALID_SOCKET == pConn->Socket))
    {
        goto EXIT_SEND;
    }

    /* WSABUF has no const form. WSASend treats the buffer as read-only on a
       non-overlapped call, so casting the qualifier away is safe here. */
    WsaBuf.buf = (PCHAR)pbBuffer;
    WsaBuf.len = cbBuffer;

    iResult = WSASend(pConn->Socket, &WsaBuf, 1, &dwSent, 0, NULL, NULL);

    if (0 == iResult)
    {
        /* A short count is normal on a non-blocking socket. Resume from the
           offset once FD_WRITE fires again. */
        *pcbSent = dwSent;
        IoResult = SOCKET_IO_OK;
        goto EXIT_SEND;
    }

    switch (WSAGetLastError())
    {
    case WSAEWOULDBLOCK:
        IoResult = SOCKET_IO_WOULD_BLOCK;
        break;

    /* WSAESHUTDOWN is this side having already shut the direction down, which
       is orderly. A reset or abort is not: the peer tore the connection down
       with data possibly still in flight, and calling that a clean EOF is how
       a relay truncates a transfer and reports success. */
    case WSAESHUTDOWN:
        IoResult = SOCKET_IO_CLOSED;
        break;

    case WSAECONNRESET:
    case WSAECONNABORTED:
        IoResult = SOCKET_IO_ERROR;
        break;

    default:
        IoResult = SOCKET_IO_ERROR;
        break;
    }

EXIT_SEND:

    return IoResult;

} /* SocketConnSend */

SOCKET_STATUS
SocketConnShutdownSend(_In_ PSOCKET_CONN pConn)
{
    SOCKET_STATUS SockStatus = SOCKET_STATUS_ERROR;

    if ((NULL == pConn) || (INVALID_SOCKET == pConn->Socket))
    {
        SockStatus = SOCKET_STATUS_INVALID_ARGS;
        goto EXIT_SHUTDOWN;
    }

    /* A failing shutdown means the peer is already gone, which the read side
       will report as a close. Nothing useful to propagate. */
    (VOID) shutdown(pConn->Socket, SD_SEND);

    SockStatus = SOCKET_STATUS_SUCCESS;

EXIT_SHUTDOWN:

    return SockStatus;

} /* SocketConnShutdownSend */

VOID SocketConnClose(_Inout_ PSOCKET_CONN pConn)
{
    if (NULL == pConn)
    {
        goto EXIT_CONN_CLOSE;
    }

    if (INVALID_SOCKET != pConn->Socket)
    {
        (VOID) closesocket(pConn->Socket);
        pConn->Socket = INVALID_SOCKET;
    }

    if (WSA_INVALID_EVENT != pConn->hEvent)
    {
        (VOID) WSACloseEvent(pConn->hEvent);
        pConn->hEvent = WSA_INVALID_EVENT;
    }

EXIT_CONN_CLOSE:

    return;

} /* SocketConnClose */

PCWSTR
SocketStatusText(_In_ SOCKET_STATUS Status)
{
    PCWSTR pwszText = SOCKTXT_UNKNOWN;

    switch (Status)
    {
    case SOCKET_STATUS_SUCCESS:
        pwszText = SOCKTXT_SUCCESS;
        break;

    case SOCKET_STATUS_ERROR:
        pwszText = SOCKTXT_ERROR;
        break;

    case SOCKET_STATUS_INVALID_ARGS:
        pwszText = SOCKTXT_INVALID_ARGS;
        break;

    case SOCKET_STATUS_BAD_MEMORY_ALLOC:
        pwszText = SOCKTXT_BAD_MEMORY_ALLOC;
        break;

    case SOCKET_STATUS_WSA_STARTUP_FAIL:
        pwszText = SOCKTXT_WSA_STARTUP_FAIL;
        break;

    case SOCKET_STATUS_RESOLVE_ADDR_ERROR:
        pwszText = SOCKTXT_RESOLVE_ADDR_ERROR;
        break;

    case SOCKET_STATUS_CREATE_SOCKET_FAIL:
        pwszText = SOCKTXT_CREATE_SOCKET_FAIL;
        break;

    case SOCKET_STATUS_CREATE_EVENT_FAIL:
        pwszText = SOCKTXT_CREATE_EVENT_FAIL;
        break;

    case SOCKET_STATUS_EVENT_SELECT_FAIL:
        pwszText = SOCKTXT_EVENT_SELECT_FAIL;
        break;

    case SOCKET_STATUS_BIND_ERROR:
        pwszText = SOCKTXT_BIND_ERROR;
        break;

    case SOCKET_STATUS_LISTEN_ERROR:
        pwszText = SOCKTXT_LISTEN_ERROR;
        break;

    case SOCKET_STATUS_ACCEPT_ERROR:
        pwszText = SOCKTXT_ACCEPT_ERROR;
        break;

    case SOCKET_STATUS_CONNECT_ERROR:
        pwszText = SOCKTXT_CONNECT_ERROR;
        break;

    case SOCKET_STATUS_TIMEOUT:
        pwszText = SOCKTXT_TIMEOUT;
        break;

    case SOCKET_STATUS_ABORTED:
        pwszText = SOCKTXT_ABORTED;
        break;

    default:
        break;
    }

    return pwszText;

} /* SocketStatusText */

static SOCKET_STATUS ResolveHostAddress(_In_z_ PCWSTR        pwszAddress,
                                        _In_z_ PCWSTR        pwszPort,
                                        _In_ BOOL            bPassive,
                                        _Outptr_ PADDRINFOW *ppaiResults)
{
    SOCKET_STATUS SockStatus = SOCKET_STATUS_ERROR;
    ADDRINFOW     aiHints    = {0};
    PADDRINFOW    paiResults = NULL;
    INT           iResult    = 0;

    if (NULL != ppaiResults)
    {
        *ppaiResults = NULL;
    }

    if ((NULL == pwszAddress) || (NULL == pwszPort) || (NULL == ppaiResults))
    {
        SockStatus = SOCKET_STATUS_INVALID_ARGS;
        goto EXIT_RESOLVE;
    }

    aiHints.ai_family   = SOCKET_RESOLVE_FAMILY;
    aiHints.ai_socktype = SOCKET_RESOLVE_SOCKTYPE;
    aiHints.ai_protocol = SOCKET_RESOLVE_PROTOCOL;

    if (bPassive)
    {
        aiHints.ai_flags = SOCKET_RESOLVE_PASSIVE_FLAGS;
    }

    /* Resolve into a local so a non-zero return can never leave a stale or
       partial list visible through the out parameter. */
    iResult = GetAddrInfoW(pwszAddress, pwszPort, &aiHints, &paiResults);

    if (0 != iResult)
    {
        SockStatus = SOCKET_STATUS_RESOLVE_ADDR_ERROR;
        goto EXIT_RESOLVE;
    }

    if (NULL == paiResults)
    {
        SockStatus = SOCKET_STATUS_RESOLVE_ADDR_ERROR;
        goto EXIT_RESOLVE;
    }

    *ppaiResults = paiResults;
    paiResults   = NULL;
    SockStatus   = SOCKET_STATUS_SUCCESS;

EXIT_RESOLVE:

    if (NULL != paiResults)
    {
        FreeAddrInfoW(paiResults);
        paiResults = NULL;
    }

    return SockStatus;

} /* ResolveHostAddress */

static SOCKET_STATUS
WaitForNetworkOrAbort(_In_ WSAEVENT hNetworkEvent, _In_ DWORD dwTimeoutMs, _In_opt_ HANDLE hAbort)
{
    SOCKET_STATUS SockStatus                     = SOCKET_STATUS_ERROR;
    WSAEVENT      ahEvents[SOCKET_WAIT_SLOT_MAX] = {0};
    DWORD         dwCount                        = 1;
    DWORD         dwWait                         = 0;

    if (WSA_INVALID_EVENT == hNetworkEvent)
    {
        SockStatus = SOCKET_STATUS_INVALID_ARGS;
        goto EXIT_WAIT;
    }

    ahEvents[SOCKET_WAIT_SLOT_NETWORK] = hNetworkEvent;

    if (NULL != hAbort)
    {
        ahEvents[SOCKET_WAIT_SLOT_ABORT] = hAbort;
        dwCount                          = SOCKET_WAIT_SLOT_MAX;
    }

    /* fAlertable FALSE: no APCs are queued to these threads, and waking for
       one would only complicate the caller's state machine. */
    dwWait = WSAWaitForMultipleEvents(dwCount, ahEvents, FALSE, dwTimeoutMs, FALSE);

    if ((WSA_WAIT_EVENT_0 + SOCKET_WAIT_SLOT_NETWORK) == dwWait)
    {
        SockStatus = SOCKET_STATUS_SUCCESS;
    }
    else if ((WSA_WAIT_EVENT_0 + SOCKET_WAIT_SLOT_ABORT) == dwWait)
    {
        SockStatus = SOCKET_STATUS_ABORTED;
    }
    else if (WSA_WAIT_TIMEOUT == dwWait)
    {
        SockStatus = SOCKET_STATUS_TIMEOUT;
    }
    else
    {
        SockStatus = SOCKET_STATUS_ERROR;
    }

EXIT_WAIT:

    return SockStatus;

} /* WaitForNetworkOrAbort */

static SOCKET_STATUS CreateEventSocket(_In_ PADDRINFOW paiEntry,
                                       _In_ LONG       lEvents,
                                       _Out_ SOCKET   *pSocket,
                                       _Out_ WSAEVENT *phEvent)
{
    SOCKET_STATUS SockStatus = SOCKET_STATUS_ERROR;
    SOCKET        sSocket    = INVALID_SOCKET;
    WSAEVENT      hEvent     = WSA_INVALID_EVENT;
    INT           iResult    = 0;

    if (NULL != pSocket)
    {
        *pSocket = INVALID_SOCKET;
    }

    if (NULL != phEvent)
    {
        *phEvent = WSA_INVALID_EVENT;
    }

    if ((NULL == paiEntry) || (NULL == pSocket) || (NULL == phEvent))
    {
        SockStatus = SOCKET_STATUS_INVALID_ARGS;
        goto EXIT_CREATE_SOCKET;
    }

    sSocket = WSASocketW(paiEntry->ai_family,
                         paiEntry->ai_socktype,
                         paiEntry->ai_protocol,
                         NULL,
                         0,
                         WSA_FLAG_OVERLAPPED);

    if (INVALID_SOCKET == sSocket)
    {
        SockStatus = SOCKET_STATUS_CREATE_SOCKET_FAIL;
        goto EXIT_CREATE_SOCKET;
    }

    hEvent = WSACreateEvent();

    if (WSA_INVALID_EVENT == hEvent)
    {
        SockStatus = SOCKET_STATUS_CREATE_EVENT_FAIL;
        goto EXIT_CREATE_SOCKET;
    }

    /* This also switches the socket to non-blocking mode, which is why every
       I/O path below has to treat WSAEWOULDBLOCK as a normal outcome. */
    iResult = WSAEventSelect(sSocket, hEvent, lEvents);

    if (SOCKET_ERROR == iResult)
    {
        SockStatus = SOCKET_STATUS_EVENT_SELECT_FAIL;
        goto EXIT_CREATE_SOCKET;
    }

    *pSocket = sSocket;
    *phEvent = hEvent;

    sSocket    = INVALID_SOCKET;
    hEvent     = WSA_INVALID_EVENT;
    SockStatus = SOCKET_STATUS_SUCCESS;

EXIT_CREATE_SOCKET:

    if (WSA_INVALID_EVENT != hEvent)
    {
        (VOID) WSACloseEvent(hEvent);
        hEvent = WSA_INVALID_EVENT;
    }

    if (INVALID_SOCKET != sSocket)
    {
        (VOID) closesocket(sSocket);
        sSocket = INVALID_SOCKET;
    }

    return SockStatus;

} /* CreateEventSocket */

/* end of file */
