/**
 * @file Snailnet.h
 * @brief Shared constants: labels, defaults, layout, limits, exit codes.
 */

#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

//
// WinSock2.h has to be pulled in ahead of Windows.h. Windows.h drags in the
// original winsock.h on its own, and the two collide on sockaddr, timeval and
// friends. Including them in this order here means every translation unit that
// takes Snailnet.h gets the right one first.
//
#include <WinSock2.h>
#include <WS2tcpip.h>
#include <Windows.h>

#define PROJ_NAME L"SnailNet"
#define WINDOW_CLASS L"SnailNetWindow"
#define FONT_HEIGHT (-17)
#define TITLE_FONT_HEIGHT (-40)
#define TITLE_FONT_FACE L"Segoe UI"

//
// Posted by a relay worker. wParam is a RELAY_NOTIFY, lParam is its data.
//
#define WM_RELAY_NOTIFY (WM_APP + 1)

#define WND_CLASS_STATIC L"STATIC"
#define WND_CLASS_EDIT L"EDIT"
#define WND_CLASS_BUTTON L"BUTTON"

#define LABEL_LISTEN_ADDR L"Listen address"
#define LABEL_LISTEN_PORT L"Listen port"
#define LABEL_TARGET_ADDR L"Target address"
#define LABEL_TARGET_PORT L"Target port"
#define LABEL_LATENCY L"Latency (ms)"
#define CAPTION_START L"Start"
#define CAPTION_STOP L"Stop"

#define DEFAULT_LISTEN_ADDR L"127.0.0.1"
#define DEFAULT_LISTEN_PORT L"8080"
#define DEFAULT_TARGET_ADDR L"127.0.0.1"
#define DEFAULT_TARGET_PORT L"80"
#define DEFAULT_LATENCY L"250"

#define MSG_IDLE L"SnailNet Idle. No active connections."
#define MSG_NEED_ADDRESSES L"Both addresses are required."
#define MSG_BAD_LATENCY L"Latency must be a whole number of milliseconds."
#define MSG_FAIL_REGISTER L"Failed to register the window class."
#define MSG_FAIL_CREATE L"Failed to create the main window."
#define MSG_FAIL_WINSOCK L"Winsock could not be initialised."
#define MSG_STOPPING L"Stopping. Waiting for active connections to finish."

#define TAB_CAPTION_RELAY L"Relay"
#define TAB_CAPTION_HELP L"Help"
#define TAB_CAPTION_ABOUT L"About"

#define FMT_BAD_PORT L"%s must be between %u and %u."
#define FMT_START_FAILED L"Could not start: %s"

// Converts a parsed port number back to text for RELAY_CONFIG. Printing the
// parsed value rather than keeping the raw field strips whatever spacing the
// user typed.
#define FMT_PORT_TEXT L"%u"

//
// The status block is several lines. One line could not hold an address
// pair, the latency and the counters at this window width, so the static
// control ellipsized the end and the session counts were never visible.
// Static controls break on CRLF, the same as the edit controls in Help.c.
//
#define FMT_RUNNING L"Listening on:  %s:%u\r\nForwarding to:  %s:%u\r\nLatency:  %u ms to client"
#define FMT_SESSIONS L"%s\r\nSessions:  %u active, %u total"
#define FMT_RELAY_ERROR L"%s\r\nError:  %s"

typedef enum _SNAILNET_LIMITS
{
    PORT_MIN        = 1,
    PORT_MAX        = 65535,
    ADDR_CCH        = 64, // characters, not bytes
    STATUS_CCH      = 192,
    STATUS_LINE_CCH = 320, // status text plus the counters appended to it
    PORT_LABEL_CCH  = 128
} SNAILNET_LIMITS,
    *PSNAILNET_LIMITS;

typedef enum _SNAILNET_CTL_ID
{
    IDC_LISTEN_ADDR = 101,
    IDC_LISTEN_PORT,
    IDC_TARGET_ADDR,
    IDC_TARGET_PORT,
    IDC_LATENCY,
    IDC_START,
    IDC_STOP,
    IDC_STATUS,
    IDC_TITLE,
    IDC_SNAIL,
    IDC_TABS,
    IDC_HELP_TEXT,
    IDC_ABOUT_TEXT,

    //
    // The field labels need identifiers of their own now. Tab switching hides
    // controls one at a time, and a control with no id cannot be found to hide.
    //
    IDC_LABEL_LISTEN_ADDR,
    IDC_LABEL_LISTEN_PORT,
    IDC_LABEL_TARGET_ADDR,
    IDC_LABEL_TARGET_PORT,
    IDC_LABEL_LATENCY
} SNAILNET_CTL_ID,
    *PSNAILNET_CTL_ID;

typedef enum _SNAILNET_TAB_INDEX
{
    TAB_RELAY = 0,
    TAB_HELP,
    TAB_ABOUT
} SNAILNET_TAB_INDEX,
    *PSNAILNET_TAB_INDEX;

typedef enum _SNAILNET_LAYOUT
{
    MARGIN     = 16,
    LABEL_W    = 150,
    FIELD_W    = 210,
    ROW_H      = 32,
    ROW_GAP    = 10,
    BUTTON_W   = 110,
    BUTTON_H   = 36,
    BUTTON_GAP = 14,
    ICON_SIZE  = 64,
    HEADER_H   = 112,
    PANEL_PAD  = 8,
    TABS_H     = 410,
    STATUS_H   = 92, // four lines: three of detail plus one for an error
    CLIENT_W   = 440,
    CLIENT_H   = 544
} SNAILNET_LAYOUT,
    *PSNAILNET_LAYOUT;

typedef enum _SNAILNET_STATUS
{
    SNAILNET_SUCCESS = 0,
    SNAILNET_ERROR,
    SNAILNET_FAIL_TO_REGISTER,
    SNAILNET_FAIL_CREATE_WINDOW,
    SNAILNET_FAIL_WINSOCK
} SNAILNET_STATUS,
    *PSNAILNET_STATUS;

LRESULT
CALLBACK
WndProc(_In_ HWND hWindow, _In_ UINT uMessage, _In_ WPARAM wParam, _In_ LPARAM lParam);

/* end of file */
