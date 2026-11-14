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

#include "Version.h"

#define PROJ_NAME L"SnailNet"
#define LABEL_VERSION L"Version " SNAILNET_VERSION_WSTR
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
#define WND_CLASS_COMBOBOX L"COMBOBOX"

#define LABEL_LISTEN_ADDR L"Listen address"
#define LABEL_LISTEN_PORT L"Listen port"
#define LABEL_TARGET_ADDR L"Target address"
#define LABEL_TARGET_PORT L"Target port"
#define LABEL_LATENCY L"Latency (ms)"
#define LABEL_DELAY_DIR L"Delay applied"
#define CAPTION_START L"Start"
#define CAPTION_STOP L"Stop"

#define DEFAULT_LISTEN_ADDR L"127.0.0.1"
#define DEFAULT_LISTEN_PORT L"8080"
#define DEFAULT_TARGET_ADDR L"127.0.0.1"
#define DEFAULT_TARGET_PORT L"80"
#define DEFAULT_LATENCY L"250"
#define DEFAULT_DELAY_DIR DELAY_DIR_TO_CLIENT

//
// One entry per SNAILNET_DELAY_DIR, in the same order. The GUI lists the
// choices with the ITEM_ labels, and both front ends describe a running
// relay with the matching TEXT_ phrase.
//
#define ITEM_DELAY_TO_CLIENT L"To client"
#define ITEM_DELAY_TO_TARGET L"To target"
#define ITEM_DELAY_BOTH L"Both ways"
#define TEXT_DELAY_TO_CLIENT L"to client"
#define TEXT_DELAY_TO_TARGET L"to target"
#define TEXT_DELAY_BOTH L"each way"

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
#define FMT_ADDR_TOO_LONG L"%s must be at most %u characters."
#define FMT_START_FAILED L"Could not start: %s"

// Converts a parsed port number back to text for RELAY_CONFIG. Printing the
// parsed value rather than keeping the raw field strips whatever spacing the
// user typed.
#define FMT_PORT_TEXT L"%u"

//
// The status block is several lines: one line cannot hold an address
// pair, the latency and the session counters at this window width.
// Static controls break on CRLF, the same as the edit controls in Help.c.
//
#define FMT_RUNNING L"Listening on:  %s:%u\r\nForwarding to:  %s:%u\r\nLatency:  %u ms %s"
#define FMT_SESSIONS L"%s\r\nSessions:  %u active, %u total"
#define FMT_RELAY_ERROR L"%s\r\nError:  %s"

/**
 * @brief Which direction the latency is applied to.
 *
 * @details The values double as indexes into the GUI's delay list, which
 * is filled in this order, so they must stay contiguous from zero.
 */
typedef enum _SNAILNET_DELAY_DIR
{
    DELAY_DIR_TO_CLIENT = 0, /**< Replies are held; requests go at once */
    DELAY_DIR_TO_TARGET,     /**< Requests are held; replies go at once */
    DELAY_DIR_BOTH,          /**< Both are held, so a round trip pays twice */
    DELAY_DIR_COUNT
} SNAILNET_DELAY_DIR,
    *PSNAILNET_DELAY_DIR;

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
    // The field labels need identifiers: tab switching hides controls one
    // at a time, and a control with no id cannot be found to hide.
    //
    IDC_LABEL_LISTEN_ADDR,
    IDC_LABEL_LISTEN_PORT,
    IDC_LABEL_TARGET_ADDR,
    IDC_LABEL_TARGET_PORT,
    IDC_LABEL_LATENCY,

    //
    // Added last so every identifier above keeps its value; the test
    // harness addresses controls by number.
    //
    IDC_VERSION,
    IDC_DELAY_DIR,
    IDC_LABEL_DELAY_DIR
} SNAILNET_CTL_ID,
    *PSNAILNET_CTL_ID;

typedef enum _SNAILNET_TAB_INDEX
{
    TAB_RELAY = 0,
    TAB_HELP,
    TAB_ABOUT
} SNAILNET_TAB_INDEX,
    *PSNAILNET_TAB_INDEX;

//
// Sizes at 96 DPI; Ui.c scales them to the window's DPI. CLIENT_W and
// CLIENT_H are the starting and smallest client area.
//
typedef enum _SNAILNET_LAYOUT
{
    MARGIN     = 16,
    LABEL_W    = 150,
    ROW_H      = 32,
    ROW_GAP    = 10,
    BUTTON_W   = 110,
    BUTTON_H   = 36,
    BUTTON_GAP = 14,
    ICON_SIZE  = 64,
    HEADER_H   = 112,
    VERSION_H  = 20, // the version line under the title, inside HEADER_H
    PANEL_PAD  = 8,
    STATUS_H   = 92, // four lines: three of detail plus one for an error
    COMBO_DROP = 4,  // rows the delay list drops down by, past its own
    CLIENT_W   = 440,
    CLIENT_H   = 586
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
