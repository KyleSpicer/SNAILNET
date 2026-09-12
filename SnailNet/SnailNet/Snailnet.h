/**
 * @file Snailnet.h
 * @brief Shared constants: labels, defaults, layout, limits, exit codes.
 */

#pragma once

#include <Windows.h>

#define PROJ_NAME L"SnailNet"
#define WINDOW_CLASS L"SnailNetWindow"
#define FONT_HEIGHT -17
#define TITLE_FONT_HEIGHT -40
#define TITLE_FONT_FACE L"Segoe UI"

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

#define FMT_BAD_PORT L"%s must be between %u and %u."
#define FMT_RUNNING L"Listening on %s:%u -> %s:%u (+%u ms)"

enum LIMITS
{
    PORT_MIN       = 1,
    PORT_MAX       = 65535,
    ADDR_CCH       = 64, /* characters, not bytes */
    STATUS_CCH     = 192,
    PORT_LABEL_CCH = 128
};

enum CTL_IDS
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
    IDC_SNAIL
};

enum LAYOUT
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
    CLIENT_W   = 440,
    CLIENT_H   = 440,
};

enum RETURNS
{
    SNAILNET_SUCCESS = 0,
    SNAILNET_ERROR,
    SNAILNET_FAIL_TO_REGISTER,
    SNAILNET_FAIL_CREATE_WINDOW
};

LRESULT CALLBACK WndProc(HWND hWindow, UINT Message, WPARAM wParam, LPARAM lParam);

/* end of file */
