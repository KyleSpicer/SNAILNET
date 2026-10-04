/**
 * @file Sockets.h
 * @brief Event-driven Winsock wrapper for TCP listeners and connections.
 *
 * @details Two types. SOCKET_CTX is a listener: it resolves an address, binds,
 * listens, and waits on FD_ACCEPT. SOCKET_CONN is a single connected socket
 * with its own event, used for both accepted and outbound connections.
 *
 * Every socket produced here is non-blocking, because WSAEventSelect puts it
 * in that mode. I/O therefore reports SOCKET_IO_WOULD_BLOCK rather than
 * stalling, and the caller waits on the connection's event before retrying.
 *
 * Waits accept an optional abort handle so a worker thread can be released
 * promptly on shutdown instead of sitting out a timeout.
 */

#pragma once

#include <WinSock2.h>
#include <WS2tcpip.h>
#include <Windows.h>

#define SOCKET_IS_SUCCESS(Status) (SOCKET_STATUS_SUCCESS == (Status))
#define SOCKET_IS_ERROR(Status) (SOCKET_STATUS_SUCCESS != (Status))

/** Event mask applied to a connected socket. */
#define SOCKET_CONN_EVENTS (FD_READ | FD_WRITE | FD_CLOSE)

/** @brief Result of a library operation. */
typedef enum _SOCKET_STATUS
{
    SOCKET_STATUS_SUCCESS            = 0,  /**< Completed successfully */
    SOCKET_STATUS_ERROR              = 1,  /**< Unclassified failure */
    SOCKET_STATUS_INVALID_ARGS       = 2,  /**< Invalid arguments */
    SOCKET_STATUS_BAD_MEMORY_ALLOC   = 3,  /**< Allocation failed */
    SOCKET_STATUS_WSA_STARTUP_FAIL   = 4,  /**< WSAStartup failed */
    SOCKET_STATUS_RESOLVE_ADDR_ERROR = 5,  /**< Address resolution failed */
    SOCKET_STATUS_CREATE_SOCKET_FAIL = 6,  /**< Socket creation failed */
    SOCKET_STATUS_CREATE_EVENT_FAIL  = 7,  /**< WSACreateEvent failed */
    SOCKET_STATUS_EVENT_SELECT_FAIL  = 8,  /**< WSAEventSelect failed */
    SOCKET_STATUS_BIND_ERROR         = 9,  /**< bind failed */
    SOCKET_STATUS_LISTEN_ERROR       = 10, /**< listen failed */
    SOCKET_STATUS_ACCEPT_ERROR       = 11, /**< accept failed */
    SOCKET_STATUS_CONNECT_ERROR      = 12, /**< connect failed or refused */
    SOCKET_STATUS_TIMEOUT            = 13, /**< Wait expired */
    SOCKET_STATUS_ABORTED            = 14, /**< Abort handle was signalled */
    SOCKET_STATUS_CLOSED             = 15  /**< Peer closed the connection */

} SOCKET_STATUS,
    *PSOCKET_STATUS;

/**
 * @brief Outcome of a single send or receive.
 *
 * @details Separate from SOCKET_STATUS because a relay must distinguish four
 * cases that all look like failure to a naive caller: progress, back pressure,
 * an orderly close, and a real error. Treating back pressure or a close as an
 * error is the usual way a proxy ends up truncating data.
 */
typedef enum _SOCKET_IO
{
    SOCKET_IO_OK          = 0, /**< Bytes moved; count is in the out param */
    SOCKET_IO_WOULD_BLOCK = 1, /**< Nothing moved; wait on the event */
    SOCKET_IO_CLOSED      = 2, /**< Peer sent FIN; no more data this way */
    SOCKET_IO_ERROR       = 3  /**< Connection is unusable */

} SOCKET_IO,
    *PSOCKET_IO;

/**
 * @brief A listening socket and its FD_ACCEPT event.
 */
typedef struct _SOCKET_CTX
{
    PADDRINFOW paiResults;    /**< Resolved bind address; owned */
    SOCKET     Socket;        /**< Listening socket */
    WSAEVENT   hNetworkEvent; /**< Signalled on FD_ACCEPT */

} SOCKET_CTX, *PSOCKET_CTX;

/**
 * @brief One connected socket and its event.
 *
 * @details Caller-owned storage, so the relay can embed it in its own
 * per-connection structure without a second allocation.
 */
typedef struct _SOCKET_CONN
{
    SOCKET           Socket;   /**< Connected socket */
    WSAEVENT         hEvent;   /**< Signalled on SOCKET_CONN_EVENTS */
    SOCKADDR_STORAGE PeerAddr; /**< Remote address, when known */
    INT              iPeerLen; /**< Bytes valid in PeerAddr */

} SOCKET_CONN, *PSOCKET_CONN;

/**
 * @brief Initialises Winsock for this module.
 *
 * @details Must be paired with SocketLibCleanup. Winsock reference counts
 * internally, so nested calls are safe.
 *
 * @return SOCKET_STATUS_SUCCESS, or SOCKET_STATUS_WSA_STARTUP_FAIL.
 */
SOCKET_STATUS
SocketLibInit(VOID);

/**
 * @brief Releases the Winsock reference taken by SocketLibInit.
 */
VOID SocketLibCleanup(VOID);

/**
 * @brief Checks that an address and port can be resolved.
 *
 * @details Resolution only. It proves the text is a name or literal the stack
 * can turn into an address, not that anything is listening there. A caller
 * wanting reachability has to attempt a connection.
 *
 * @param pwszAddress Address text to test.
 * @param pwszPort    Port text to test.
 *
 * @return SOCKET_STATUS_SUCCESS, or SOCKET_STATUS_RESOLVE_ADDR_ERROR.
 */
SOCKET_STATUS
SocketResolveCheck(_In_z_ PCWSTR pwszAddress, _In_z_ PCWSTR pwszPort);

/**
 * @brief Allocates a listener context.
 *
 * @param ppSocketCtx Receives the context. Must point to NULL.
 *
 * @return SOCKET_STATUS_SUCCESS on success, error code on failure.
 */
SOCKET_STATUS
SocketContextCreate(_Inout_ PSOCKET_CTX *ppSocketCtx);

/**
 * @brief Resolves the bind address and creates the listening socket.
 *
 * @param pSocketCtx  Context to initialise.
 * @param pwszAddress Local address to bind, e.g. L"127.0.0.1".
 * @param pwszPort    Local port as text, e.g. L"8080".
 *
 * @return SOCKET_STATUS_SUCCESS on success, error code on failure.
 */
SOCKET_STATUS
SocketContextInit(_Inout_ PSOCKET_CTX pSocketCtx,
                  _In_z_ PCWSTR       pwszAddress,
                  _In_z_ PCWSTR       pwszPort);

/**
 * @brief Closes the socket, frees the address list, and frees the context.
 *
 * @details Safe to call after a partially failed init.
 *
 * @param ppSocketCtx Context to release. Set to NULL on return.
 *
 * @return SOCKET_STATUS_SUCCESS on success, error code on failure.
 */
SOCKET_STATUS
SocketContextCleanup(_Inout_ PSOCKET_CTX *ppSocketCtx);

/**
 * @brief Binds, listens, and arms the FD_ACCEPT event.
 *
 * @param pSocketCtx Initialised listener context.
 *
 * @return SOCKET_STATUS_SUCCESS on success, error code on failure.
 */
SOCKET_STATUS
SocketBindAndListen(_In_ PSOCKET_CTX pSocketCtx);

/**
 * @brief Waits for and accepts one inbound connection.
 *
 * @details The accepted socket is given its own event and SOCKET_CONN_EVENTS
 * mask, which replaces the FD_ACCEPT association it inherits from the
 * listener. Forgetting that inheritance is a common source of connections that
 * appear to hang.
 *
 * @param pSocketCtx  Listening context.
 * @param dwTimeoutMs Wait limit, or INFINITE.
 * @param hAbort      Optional event that cancels the wait.
 * @param pConn       Receives the accepted connection.
 *
 * @return SOCKET_STATUS_SUCCESS, SOCKET_STATUS_TIMEOUT,
 *         SOCKET_STATUS_ABORTED, or an error code.
 */
SOCKET_STATUS
SocketAccept(_In_ PSOCKET_CTX   pSocketCtx,
             _In_ DWORD         dwTimeoutMs,
             _In_opt_ HANDLE    hAbort,
             _Out_ PSOCKET_CONN pConn);

/**
 * @brief Zeroes a connection structure to a safe, closed state.
 *
 * @param pConn Connection to initialise.
 *
 * @return SOCKET_STATUS_SUCCESS on success, error code on failure.
 */
SOCKET_STATUS
SocketConnInit(_Out_ PSOCKET_CONN pConn);

/**
 * @brief Opens an outbound connection.
 *
 * @details The connect is issued non-blocking and completed by waiting for
 * FD_CONNECT, so a slow or unreachable target does not pin the calling thread
 * past the timeout or the abort signal.
 *
 * @param pConn       Connection to fill in. Initialised on entry.
 * @param pwszAddress Target address.
 * @param pwszPort    Target port as text.
 * @param dwTimeoutMs Wait limit, or INFINITE.
 * @param hAbort      Optional event that cancels the wait.
 *
 * @return SOCKET_STATUS_SUCCESS, SOCKET_STATUS_TIMEOUT,
 *         SOCKET_STATUS_ABORTED, or an error code.
 */
SOCKET_STATUS
SocketConnConnect(_Inout_ PSOCKET_CONN pConn,
                  _In_z_ PCWSTR        pwszAddress,
                  _In_z_ PCWSTR        pwszPort,
                  _In_ DWORD           dwTimeoutMs,
                  _In_opt_ HANDLE      hAbort);

/**
 * @brief Waits for network activity on a connection.
 *
 * @details Retrieves and clears the pending events with WSAEnumNetworkEvents,
 * so the caller must act on every bit returned in one pass; they are not
 * reported twice.
 *
 * @param pConn       Connection to wait on.
 * @param dwTimeoutMs Wait limit, or INFINITE.
 * @param hAbort      Optional event that cancels the wait.
 * @param plEvents    Receives the FD_* bits that fired. Zero on timeout.
 *
 * @return SOCKET_STATUS_SUCCESS, SOCKET_STATUS_TIMEOUT,
 *         SOCKET_STATUS_ABORTED, or an error code.
 */
SOCKET_STATUS
SocketConnWait(_In_ PSOCKET_CONN pConn,
               _In_ DWORD        dwTimeoutMs,
               _In_opt_ HANDLE   hAbort,
               _Out_ PLONG       plEvents);

/**
 * @brief Reads whatever is available, without waiting.
 *
 * @param pConn    Connection to read from.
 * @param pbBuffer Destination buffer.
 * @param cbBuffer Size of pbBuffer in bytes.
 * @param pcbRead  Receives the byte count. Zero unless SOCKET_IO_OK.
 *
 * @return SOCKET_IO_OK, SOCKET_IO_WOULD_BLOCK, SOCKET_IO_CLOSED,
 *         or SOCKET_IO_ERROR.
 */
SOCKET_IO
SocketConnRecv(_In_ PSOCKET_CONN                  pConn,
               _Out_writes_bytes_(cbBuffer) PBYTE pbBuffer,
               _In_ DWORD                         cbBuffer,
               _Out_ PDWORD                       pcbRead);

/**
 * @brief Writes what the socket will take, without waiting.
 *
 * @details A partial write is normal and reports SOCKET_IO_OK with a count
 * below cbBuffer. Resume from that offset after the next FD_WRITE.
 *
 * @param pConn    Connection to write to.
 * @param pbBuffer Source buffer.
 * @param cbBuffer Bytes to send.
 * @param pcbSent  Receives the byte count. Zero unless SOCKET_IO_OK.
 *
 * @return SOCKET_IO_OK, SOCKET_IO_WOULD_BLOCK, SOCKET_IO_CLOSED,
 *         or SOCKET_IO_ERROR.
 */
SOCKET_IO
SocketConnSend(_In_ PSOCKET_CONN                  pConn,
               _In_reads_bytes_(cbBuffer) LPCBYTE pbBuffer,
               _In_ DWORD                         cbBuffer,
               _Out_ PDWORD                       pcbSent);

/**
 * @brief Sends FIN, leaving the receive direction open.
 *
 * @details Call this once a direction has drained so the peer sees an orderly
 * close while data still queued the other way continues to flow.
 *
 * @param pConn Connection to half-close.
 *
 * @return SOCKET_STATUS_SUCCESS on success, error code on failure.
 */
SOCKET_STATUS
SocketConnShutdownSend(_In_ PSOCKET_CONN pConn);

/**
 * @brief Closes the socket and its event.
 *
 * @details Idempotent; safe on an already closed or never opened connection.
 *
 * @param pConn Connection to close.
 */
VOID SocketConnClose(_Inout_ PSOCKET_CONN pConn);

/**
 * @brief Maps a status code to fixed English text for logging.
 *
 * @param Status Code to describe.
 *
 * @return A static string. Never NULL.
 */
PCWSTR
SocketStatusText(_In_ SOCKET_STATUS Status);

/* end of file */
