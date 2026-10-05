/**
 * @file Relay.h
 * @brief Listener and per-connection workers that forward bytes, late.
 *
 * @details One accept thread owns the listening socket. Every client it
 * accepts gets a session thread that opens its own connection to the target
 * and pumps both directions until either side closes.
 *
 * The delay is one way. Bytes travelling from the target back out to the
 * client are held; bytes on their way in to the target are forwarded as soon
 * as they arrive.
 *
 * The delay is applied by stamping each chunk with a due time as it arrives
 * and holding it in a queue until that time passes. Sleeping on the read
 * instead would add the same latency but would also cap throughput at one
 * buffer per interval, which is not what a network delay looks like.
 *
 * Workers never touch a control, and this module knows nothing about
 * windows or consoles. They report progress through a caller supplied
 * RELAY_NOTIFY_PROC callback instead, so the same engine serves the GUI
 * (which posts a message and returns) and the CLI (which prints and
 * returns) without either knowing about the other.
 */

#pragma once

/* WinSock2.h must be seen before Windows.h, which would otherwise pull in the
   original winsock.h and collide with it. */
#include <WinSock2.h>
#include <Windows.h>

#include "Snailnet.h"
#include "Sockets.h"

typedef enum _RELAY_LIMITS
{
    RELAY_ADDR_CCH     = 64,      /**< Characters in an address field */
    RELAY_PORT_CCH     = 8,       /**< Characters in a port field */
    RELAY_MAX_SESSIONS = 64,      /**< Concurrent clients; MAXIMUM_WAIT_OBJECTS */
    RELAY_CHUNK_CB     = 16384,   /**< Bytes read per recv */
    RELAY_QUEUE_MAX_CB = 1048576, /**< Back pressure limit per direction */
    RELAY_ACCEPT_MS    = 250,     /**< Accept wait slice */
    RELAY_CONNECT_MS   = 10000,   /**< Limit on dialling the target */
    RELAY_NO_DELAY     = 0,       /**< Latency for an undelayed direction */
    RELAY_DRAIN_MS     = 5000,    /**< Total time allowed to flush a queue */
    RELAY_DRAIN_SLICE  = 100,     /**< Retry interval while flushing */

    /** Upper bound on how long SessionPump ever waits on the session's events
        in one pass, even when both directions look like they have nothing to
        do. Winsock only re-arms FD_READ or FD_WRITE for a socket once a recv()
        or send() on it has actually returned WSAEWOULDBLOCK; skipping that
        call (as DirectionRead does once a queue is at RELAY_QUEUE_MAX_CB, or
        DirectionWrite does while the head chunk is not yet due) while still
        polling the socket's event for the other direction can consume a
        pending notification that call was supposed to earn, leaving nothing
        to regenerate it and the session stalled until the peer gives up. Small
        enough that the periodic re-poll costs nothing worth measuring against
        RELAY_MAX_SESSIONS concurrent sessions, and small enough to keep it
        well under the throughput a real transfer needs. */
    RELAY_MAX_PUMP_WAIT_MS = 20

} RELAY_LIMITS,
    *PRELAY_LIMITS;

/** @brief What a worker is telling the UI. */
typedef enum _RELAY_NOTIFY
{
    RELAY_NOTIFY_LISTENING = 1, /**< Listener is bound and accepting */
    RELAY_NOTIFY_ACCEPTED  = 2, /**< A client session started */
    RELAY_NOTIFY_CLOSED    = 3, /**< A client session ended */
    RELAY_NOTIFY_ERROR     = 4, /**< lParam holds a SOCKET_STATUS */
    RELAY_NOTIFY_STOPPED   = 5  /**< Every worker has exited */

} RELAY_NOTIFY,
    *PRELAY_NOTIFY;

/**
 * @brief Callback a relay uses to report progress.
 *
 * @details Called directly from whichever worker thread has the news (the
 * accept thread for RELAY_NOTIFY_LISTENING and RELAY_NOTIFY_STOPPED, a
 * session thread for everything else), never posted or queued. The
 * callback must not block: it is the caller's one chance to hop to
 * whatever thread or output it owns, the same way the GUI's implementation
 * only calls PostMessageW and returns.
 *
 * @param pContext Opaque value passed through unchanged from RelayStart.
 * @param Code     What happened.
 * @param lData    Code specific payload.
 */
typedef VOID(CALLBACK *RELAY_NOTIFY_PROC)(_In_opt_ PVOID    pContext,
                                          _In_ RELAY_NOTIFY Code,
                                          _In_ LPARAM       lData);

/**
 * @brief Everything a relay needs to run.
 *
 * @details Ports are kept as text because the resolver wants text, and
 * converting once here beats converting on every connection.
 */
typedef struct _RELAY_CONFIG
{
    WCHAR wszListenAddr[RELAY_ADDR_CCH]; /**< Local bind address */
    WCHAR wszListenPort[RELAY_PORT_CCH]; /**< Local bind port */
    WCHAR wszTargetAddr[RELAY_ADDR_CCH]; /**< Address to forward to */
    WCHAR wszTargetPort[RELAY_PORT_CCH]; /**< Port to forward to */
    DWORD dwLatencyMs;                   /**< Delay added to each direction */

} RELAY_CONFIG, *PRELAY_CONFIG;

/**
 * @brief Binds the listener and starts the accept thread.
 *
 * @details Returns once the socket is listening, so a bind failure reaches the
 * caller as a status code rather than a notification.
 *
 * @param pConfig         Configuration to copy. Not referenced after this
 *                        returns.
 * @param pfnNotify       Callback for progress reports. May be NULL.
 * @param pNotifyContext  Opaque value passed back to pfnNotify unchanged.
 *                        Meaningful only together with pfnNotify.
 *
 * @return SOCKET_STATUS_SUCCESS on success, error code on failure.
 */
SOCKET_STATUS
RelayStart(_In_ CONST RELAY_CONFIG   *pConfig,
           _In_opt_ RELAY_NOTIFY_PROC pfnNotify,
           _In_opt_ PVOID             pNotifyContext);

/**
 * @brief Signals every worker and waits for them to exit.
 *
 * @details Idempotent, and safe to call when nothing is running. Blocks until
 * the last session thread is gone, so it must not be called from a worker.
 */
VOID RelayStop(VOID);

/* end of file */
