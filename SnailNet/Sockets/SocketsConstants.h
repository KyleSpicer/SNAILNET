/**
 * @file SocketsConstants.h
 * @brief Constants for the Sockets library: Winsock settings, wait slots,
 * status text, and the helper macros the public API exposes.
 *
 * @details Every name carries the library's SOCKET_ or SOCKTXT_ prefix.
 * Sockets.h includes this file, so these names reach every caller, and an
 * unprefixed name would collide with the caller's own (Relay.c has its own
 * WAIT_SLOT_* set, for example).
 */

#pragma once

#include <WinSock2.h>
#include <WS2tcpip.h>
#include <Windows.h>

/** True for any SOCKET_STATUS other than success. */
#define SOCKET_IS_ERROR(Status) (SOCKET_STATUS_SUCCESS != (Status))

/** Event mask applied to a connected socket. */
#define SOCKET_CONN_EVENTS (FD_READ | FD_WRITE | FD_CLOSE)

/** Winsock version requested from WSAStartup. */
#define SOCKET_WINSOCK_VERSION_MAJOR 2
#define SOCKET_WINSOCK_VERSION_MINOR 2

/** Pending connection queue length for a listener. */
#define SOCKET_LISTEN_BACKLOG SOMAXCONN

/** Address resolution hints: TCP over IPv4 or IPv6, whichever the address
    resolves to. Listeners add SOCKET_RESOLVE_PASSIVE_FLAGS so an empty
    address binds every interface. */
#define SOCKET_RESOLVE_FAMILY AF_UNSPEC
#define SOCKET_RESOLVE_SOCKTYPE SOCK_STREAM
#define SOCKET_RESOLVE_PROTOCOL IPPROTO_TCP
#define SOCKET_RESOLVE_PASSIVE_FLAGS AI_PASSIVE

/** Slots in the array handed to WSAWaitForMultipleEvents. */
#define SOCKET_WAIT_SLOT_NETWORK 0
#define SOCKET_WAIT_SLOT_ABORT 1
#define SOCKET_WAIT_SLOT_MAX 2

/** Fixed English text returned by SocketStatusText, one per SOCKET_STATUS
    value plus the fallback for a code outside the enum. */
#define SOCKTXT_UNKNOWN L"Unknown socket status."
#define SOCKTXT_SUCCESS L"Success."
#define SOCKTXT_ERROR L"The operation failed."
#define SOCKTXT_INVALID_ARGS L"Invalid arguments."
#define SOCKTXT_BAD_MEMORY_ALLOC L"Out of memory."
#define SOCKTXT_WSA_STARTUP_FAIL L"Winsock could not be initialised."
#define SOCKTXT_RESOLVE_ADDR_ERROR L"The address could not be resolved."
#define SOCKTXT_CREATE_SOCKET_FAIL L"The socket could not be created."
#define SOCKTXT_CREATE_EVENT_FAIL L"The network event could not be created."
#define SOCKTXT_EVENT_SELECT_FAIL L"The socket could not be bound to its event."
#define SOCKTXT_BIND_ERROR L"The address or port is unavailable."
#define SOCKTXT_LISTEN_ERROR L"The socket could not listen."
#define SOCKTXT_ACCEPT_ERROR L"The connection could not be accepted."
#define SOCKTXT_CONNECT_ERROR L"The target refused the connection or is unreachable."
#define SOCKTXT_TIMEOUT L"The operation timed out."
#define SOCKTXT_ABORTED L"The operation was cancelled."

/* end of file */
