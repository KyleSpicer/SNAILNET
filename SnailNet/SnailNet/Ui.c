/**
 * @file Ui.c
 * @brief Main window: class registration, tabs, controls, fonts, window proc.
 *
 * Owns everything the window is made of. What happens when a button is
 * pressed lives in Commands.c; wWinMain only creates the window and runs the
 * message loop.
 *
 * The tab control draws a header and a frame and nothing else. It does not
 * contain or hide anything, so this module owns which controls are visible
 * for the selected tab. Every control is a direct child of the main window,
 * which is what keeps GetDlgItem working for Commands.c; parenting them to
 * per-tab panels would break every lookup in that file.
 *
 * Controls are created without a position. LayoutControls places them from
 * the client size and DPI on every WM_SIZE, so the window can be resized and
 * moved between monitors.
 */

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#include <Windows.h>
#include <CommCtrl.h>

#include "Commands.h"
#include "Help.h"
#include "Resource.h"
#include "Snailnet.h"
#include "Ui.h"

#define COUNT_OF(a) (sizeof(a) / sizeof((a)[0]))

/** One labelled edit box on the Relay tab. */
typedef struct _FIELD_DEF
{
    PCWSTR pszLabel;
    INT    iLabelId;
    INT    iCtlId;
    PCWSTR pszInitial;
} FIELD_DEF, *PFIELD_DEF;

static HFONT g_hTitleFont = NULL;
static HFONT g_hUiFont    = NULL;
static HICON g_hSnailIcon = NULL;

/** The Relay tab's fields, top to bottom. */
static CONST FIELD_DEF g_aFields[] = {
    {LABEL_LISTEN_ADDR, IDC_LABEL_LISTEN_ADDR, IDC_LISTEN_ADDR, DEFAULT_LISTEN_ADDR},
    {LABEL_LISTEN_PORT, IDC_LABEL_LISTEN_PORT, IDC_LISTEN_PORT, DEFAULT_LISTEN_PORT},
    {LABEL_TARGET_ADDR, IDC_LABEL_TARGET_ADDR, IDC_TARGET_ADDR, DEFAULT_TARGET_ADDR},
    {LABEL_TARGET_PORT, IDC_LABEL_TARGET_PORT, IDC_TARGET_PORT, DEFAULT_TARGET_PORT},
    {LABEL_LATENCY, IDC_LABEL_LATENCY, IDC_LATENCY, DEFAULT_LATENCY}};

/** Controls belonging to each tab, shown and hidden as a group. */
static CONST INT g_aiRelayCtls[] = {IDC_SNAIL,
                                    IDC_TITLE,
                                    IDC_LABEL_LISTEN_ADDR,
                                    IDC_LISTEN_ADDR,
                                    IDC_LABEL_LISTEN_PORT,
                                    IDC_LISTEN_PORT,
                                    IDC_LABEL_TARGET_ADDR,
                                    IDC_TARGET_ADDR,
                                    IDC_LABEL_TARGET_PORT,
                                    IDC_TARGET_PORT,
                                    IDC_LABEL_LATENCY,
                                    IDC_LATENCY,
                                    IDC_START,
                                    IDC_STOP};

static CONST INT g_aiHelpCtls[]  = {IDC_HELP_TEXT};
static CONST INT g_aiAboutCtls[] = {IDC_ABOUT_TEXT};

/**
 * @brief Fills in the class struct and registers it.
 *
 * @param hInstance Module instance that owns the class. May be NULL.
 *
 * @return TRUE if the class registered.
 */
static BOOL RegisterMainClass(_In_opt_ HINSTANCE hInstance);

/**
 * @brief Creates the tab control and its three tabs.
 *
 * @param hParent Main window. May be NULL.
 */
static VOID AddTabs(_In_opt_ HWND hParent);

/**
 * @brief Shows one tab's controls and hides the others.
 *
 * @param hWindow Main window. May be NULL.
 * @param iTab    Index from SNAILNET_TAB_INDEX.
 */
static VOID ShowTab(_In_opt_ HWND hWindow, _In_ INT iTab);

/**
 * @brief Shows or hides a list of controls by identifier.
 *
 * @param hWindow Main window. May be NULL.
 * @param piIds   Identifiers to act on. May be NULL.
 * @param cIds    Number of identifiers.
 * @param bShow   TRUE to show, FALSE to hide.
 */
static VOID
ShowCtls(_In_opt_ HWND hWindow, _In_opt_ CONST INT *piIds, _In_ SIZE_T cIds, _In_ BOOL bShow);

/**
 * @brief Creates one child control with no position; LayoutControls places it.
 *
 * @param hParent   Main window. May be NULL.
 * @param dwExStyle Extended window style.
 * @param pszClass  Window class name.
 * @param pszText   Initial text. May be NULL.
 * @param dwStyle   Window style.
 * @param iCtlId    Control identifier.
 */
static VOID AddCtl(_In_opt_ HWND     hParent,
                   _In_ DWORD        dwExStyle,
                   _In_z_ PCWSTR     pszClass,
                   _In_opt_z_ PCWSTR pszText,
                   _In_ DWORD        dwStyle,
                   _In_ INT          iCtlId);

/**
 * @brief Scales a 96 DPI layout value to a DPI.
 *
 * @param iValue Value at 96 DPI.
 * @param uDpi   Target DPI.
 *
 * @return The scaled value.
 */
static INT Scale(_In_ INT iValue, _In_ UINT uDpi);

/**
 * @brief Gets the window size that gives a CLIENT_W x CLIENT_H client area.
 *
 * @param hWindow Main window. May be NULL.
 * @param pSize   Receives the size, scaled to the window's DPI. Zero if
 *                hWindow is NULL. May be NULL.
 */
static VOID GetMinWindowSize(_In_opt_ HWND hWindow, _Out_opt_ PSIZE pSize);

/**
 * @brief Moves and sizes one control without repainting it.
 *
 * @param hWindow Main window. May be NULL.
 * @param iCtlId  Control identifier.
 * @param x       Left edge.
 * @param y       Top edge.
 * @param cx      Width.
 * @param cy      Height.
 */
static VOID
PlaceCtl(_In_opt_ HWND hWindow, _In_ INT iCtlId, _In_ INT x, _In_ INT y, _In_ INT cx, _In_ INT cy);

/**
 * @brief Positions every control for the current client size and DPI.
 *
 * @param hWindow Main window. May be NULL.
 */
static VOID LayoutControls(_In_opt_ HWND hWindow);

/**
 * @brief EnumChildWindows callback that applies one font.
 *
 * @param hChild Control receiving the font. May be NULL.
 * @param lpFont HFONT cast to LPARAM.
 *
 * @return Always TRUE, to continue enumeration.
 */
static BOOL CALLBACK ApplyFontToChild(_In_opt_ HWND hChild, _In_ LPARAM lpFont);

/**
 * @brief Replaces the default bitmap font on every control with the shell font.
 *
 * @param hWindow Main window. May be NULL.
 */
static VOID ApplyUiFont(_In_opt_ HWND hWindow);

/**
 * @brief Gives the banner its own larger face.
 *
 * Must run after ApplyUiFont, which walks every child and would otherwise
 * overwrite it.
 *
 * @param hWindow Main window. May be NULL.
 */
static VOID ApplyTitleFont(_In_opt_ HWND hWindow);

/**
 * @brief Loads the snail at the window's DPI and puts it in the header.
 *
 * @param hWindow Main window. May be NULL.
 */
static VOID ApplySnailIcon(_In_opt_ HWND hWindow);

/**
 * @brief Recreates the fonts and the snail for the window's current DPI.
 *
 * @param hWindow Main window. May be NULL.
 */
static VOID ApplyDpiResources(_In_opt_ HWND hWindow);

/**
 * @brief Creates every child control and applies the fonts.
 *
 * @param hWindow Main window. May be NULL.
 */
static VOID BuildControls(_In_opt_ HWND hWindow);

HWND WindowCreate(_In_opt_ HINSTANCE hInstance, _In_ INT iShowCommand, _Out_opt_ PINT piStatus)
{
    INITCOMMONCONTROLSEX Controls   = {0};
    SIZE                 WindowSize = {0};
    HWND                 hWindow    = NULL;
    INT                  iStatus    = SNAILNET_SUCCESS;

    if (NULL == hInstance)
    {
        iStatus = SNAILNET_ERROR;
        goto EXIT_CREATE;
    }

    /* The tab control lives in comctl32 and will not create without this.
       A failure leaves the window usable but tabless, so it is not fatal. */
    Controls.dwSize = sizeof(Controls);
    Controls.dwICC  = ICC_TAB_CLASSES;
    (VOID) InitCommonControlsEx(&Controls);

    if (!RegisterMainClass(hInstance))
    {
        MessageBoxW(NULL, MSG_FAIL_REGISTER, PROJ_NAME, MB_ICONERROR);
        iStatus = SNAILNET_FAIL_TO_REGISTER;
        goto EXIT_CREATE;
    }

    hWindow = CreateWindowExW(0,
                              WINDOW_CLASS,
                              PROJ_NAME,
                              WS_OVERLAPPEDWINDOW,
                              CW_USEDEFAULT,
                              CW_USEDEFAULT,
                              CW_USEDEFAULT,
                              CW_USEDEFAULT,
                              NULL,
                              NULL,
                              hInstance,
                              NULL);

    if (NULL == hWindow)
    {
        MessageBoxW(NULL, MSG_FAIL_CREATE, PROJ_NAME, MB_ICONERROR);
        iStatus = SNAILNET_FAIL_CREATE_WINDOW;
        goto EXIT_CREATE;
    }

    /* Sized only now, once the window is on a monitor and has its DPI. */
    GetMinWindowSize(hWindow, &WindowSize);
    (VOID) SetWindowPos(hWindow,
                        NULL,
                        0,
                        0,
                        WindowSize.cx,
                        WindowSize.cy,
                        SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);

    ShowWindow(hWindow, iShowCommand);
    UpdateWindow(hWindow);

EXIT_CREATE:

    if (NULL != piStatus)
    {
        *piStatus = iStatus;
    }

    return hWindow;

} /* WindowCreate */

LRESULT
CALLBACK
WndProc(_In_ HWND hWindow, _In_ UINT uMessage, _In_ WPARAM wParam, _In_ LPARAM lParam)
{
    switch (uMessage)
    {
    case WM_CREATE:
        BuildControls(hWindow);
        SetRunning(hWindow, FALSE);
        ShowTab(hWindow, TAB_RELAY);
        return 0;

    case WM_SIZE:
        LayoutControls(hWindow);
        return 0;

    case WM_GETMINMAXINFO:
    {
        LPMINMAXINFO pInfo   = (LPMINMAXINFO)lParam;
        SIZE         MinSize = {0};

        GetMinWindowSize(hWindow, &MinSize);

        if (NULL != pInfo)
        {
            pInfo->ptMinTrackSize.x = MinSize.cx;
            pInfo->ptMinTrackSize.y = MinSize.cy;
        }

        return 0;
    }

    case WM_DPICHANGED:
    {
        CONST RECT *pSuggested = (CONST RECT *)lParam;

        /* The resize below sends WM_SIZE, which lays out at the new DPI. */
        ApplyDpiResources(hWindow);

        if (NULL != pSuggested)
        {
            (VOID) SetWindowPos(hWindow,
                                NULL,
                                pSuggested->left,
                                pSuggested->top,
                                pSuggested->right - pSuggested->left,
                                pSuggested->bottom - pSuggested->top,
                                SWP_NOZORDER | SWP_NOACTIVATE);
        }

        return 0;
    }

    case WM_COMMAND:
        switch (LOWORD(wParam))
        {
        case IDC_START:
            OnStart(hWindow);
            return 0;

        case IDC_STOP:
            OnStop(hWindow);
            return 0;
        }
        break;

    case WM_NOTIFY:
    {
        LPNMHDR pHeader = (LPNMHDR)lParam;

        if ((NULL != pHeader) && (IDC_TABS == (INT)pHeader->idFrom) &&
            (TCN_SELCHANGE == pHeader->code))
        {
            ShowTab(hWindow, TabCtrl_GetCurSel(pHeader->hwndFrom));
            return 0;
        }

        break;
    }

    case WM_RELAY_NOTIFY:
        /* Workers post here rather than writing to the controls themselves,
           so every update to the status line happens on this thread. */
        OnRelayNotify(hWindow, wParam, lParam);
        return 0;

    case WM_CTLCOLORSTATIC:
    {
        INT iCtlId = GetDlgCtrlID((HWND)lParam);

        /* The help and about bodies are read only edits, which also send
           this message. Painting them transparent over the dialog brush
           smears the text as they scroll, so they are left alone. */
        if ((IDC_HELP_TEXT == iCtlId) || (IDC_ABOUT_TEXT == iCtlId))
        {
            break;
        }

        /* Let labels share the dialog background instead of painting white. */
        SetBkMode((HDC)wParam, TRANSPARENT);
        return (LRESULT)GetSysColorBrush(COLOR_BTNFACE);
    }

    case WM_DESTROY:
        /* Before the fonts go, and before the message loop ends: this blocks
           until every worker is gone, and they post to this window. */
        OnShutdown();

        if (NULL != g_hTitleFont)
        {
            DeleteObject(g_hTitleFont);
            g_hTitleFont = NULL;
        }

        if (NULL != g_hUiFont)
        {
            DeleteObject(g_hUiFont);
            g_hUiFont = NULL;
        }

        if (NULL != g_hSnailIcon)
        {
            DestroyIcon(g_hSnailIcon);
            g_hSnailIcon = NULL;
        }

        PostQuitMessage(SNAILNET_SUCCESS);
        return 0;
    }

    return DefWindowProcW(hWindow, uMessage, wParam, lParam);

} /* WndProc */

static BOOL RegisterMainClass(_In_opt_ HINSTANCE hInstance)
{
    WNDCLASSEXW WindowClass = {0};
    BOOL        bResult     = FALSE;

    if (NULL == hInstance)
    {
        goto EXIT_REGISTER;
    }

    WindowClass.cbSize        = sizeof(WindowClass);
    WindowClass.lpfnWndProc   = WndProc;
    WindowClass.hInstance     = hInstance;
    WindowClass.hCursor       = LoadCursorW(NULL, IDC_ARROW);
    WindowClass.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
    WindowClass.lpszClassName = WINDOW_CLASS;

    /* LoadImageW rather than LoadIconW so we get the exact size instead of a
       scaled system default. LR_SHARED because the class outlives every local
       reference to these handles and nothing here ever destroys them. A NULL
       result is acceptable: Windows falls back to the default application
       icon. */
    WindowClass.hIcon = (HICON)LoadImageW(hInstance,
                                          MAKEINTRESOURCEW(IDI_SNAIL),
                                          IMAGE_ICON,
                                          GetSystemMetrics(SM_CXICON),
                                          GetSystemMetrics(SM_CYICON),
                                          LR_DEFAULTCOLOR | LR_SHARED);

    WindowClass.hIconSm = (HICON)LoadImageW(hInstance,
                                            MAKEINTRESOURCEW(IDI_SNAIL),
                                            IMAGE_ICON,
                                            GetSystemMetrics(SM_CXSMICON),
                                            GetSystemMetrics(SM_CYSMICON),
                                            LR_DEFAULTCOLOR | LR_SHARED);

    bResult = (0 != RegisterClassExW(&WindowClass));

EXIT_REGISTER:

    return bResult;

} /* RegisterMainClass */

static VOID AddTabs(_In_opt_ HWND hParent)
{
    TCITEMW Item  = {0};
    HWND    hTabs = NULL;

    if (NULL == hParent)
    {
        goto EXIT_ADD_TABS;
    }

    hTabs = CreateWindowExW(0,
                            WC_TABCONTROLW,
                            NULL,
                            WS_CHILD | WS_VISIBLE | WS_TABSTOP | WS_CLIPSIBLINGS,
                            0,
                            0,
                            0,
                            0,
                            hParent,
                            (HMENU)(INT_PTR)IDC_TABS,
                            NULL,
                            NULL);

    if (NULL == hTabs)
    {
        goto EXIT_ADD_TABS;
    }

    Item.mask = TCIF_TEXT;

    /* pszText is not const in TCITEMW even though the control only reads it,
       so the cast is the API's fault rather than a hidden mutation. */
    Item.pszText = (PWSTR)TAB_CAPTION_RELAY;
    (VOID) TabCtrl_InsertItem(hTabs, TAB_RELAY, &Item);

    Item.pszText = (PWSTR)TAB_CAPTION_HELP;
    (VOID) TabCtrl_InsertItem(hTabs, TAB_HELP, &Item);

    Item.pszText = (PWSTR)TAB_CAPTION_ABOUT;
    (VOID) TabCtrl_InsertItem(hTabs, TAB_ABOUT, &Item);

EXIT_ADD_TABS:

    return;

} /* AddTabs */

static VOID
ShowCtls(_In_opt_ HWND hWindow, _In_opt_ CONST INT *piIds, _In_ SIZE_T cIds, _In_ BOOL bShow)
{
    HWND   hCtl   = NULL;
    SIZE_T iIndex = 0;

    if ((NULL == hWindow) || (NULL == piIds))
    {
        goto EXIT_SHOW_CTLS;
    }

    for (iIndex = 0; iIndex < cIds; iIndex++)
    {
        hCtl = GetDlgItem(hWindow, piIds[iIndex]);

        if (NULL != hCtl)
        {
            ShowWindow(hCtl, bShow ? SW_SHOW : SW_HIDE);
        }
    }

EXIT_SHOW_CTLS:

    return;

} /* ShowCtls */

static VOID ShowTab(_In_opt_ HWND hWindow, _In_ INT iTab)
{
    if (NULL == hWindow)
    {
        goto EXIT_SHOW_TAB;
    }

    ShowCtls(hWindow, g_aiRelayCtls, COUNT_OF(g_aiRelayCtls), (TAB_RELAY == iTab));
    ShowCtls(hWindow, g_aiHelpCtls, COUNT_OF(g_aiHelpCtls), (TAB_HELP == iTab));
    ShowCtls(hWindow, g_aiAboutCtls, COUNT_OF(g_aiAboutCtls), (TAB_ABOUT == iTab));

EXIT_SHOW_TAB:

    return;

} /* ShowTab */

static VOID AddCtl(_In_opt_ HWND     hParent,
                   _In_ DWORD        dwExStyle,
                   _In_z_ PCWSTR     pszClass,
                   _In_opt_z_ PCWSTR pszText,
                   _In_ DWORD        dwStyle,
                   _In_ INT          iCtlId)
{
    if (NULL == hParent)
    {
        goto EXIT_ADD_CTL;
    }

    /* A control that fails to create leaves a gap rather than stopping the
       rest of the window, so the result is not checked. */
    CreateWindowExW(dwExStyle,
                    pszClass,
                    pszText,
                    dwStyle,
                    0,
                    0,
                    0,
                    0,
                    hParent,
                    (HMENU)(INT_PTR)iCtlId,
                    NULL,
                    NULL);

EXIT_ADD_CTL:

    return;

} /* AddCtl */

static INT Scale(_In_ INT iValue, _In_ UINT uDpi)
{
    return MulDiv(iValue, (INT)uDpi, USER_DEFAULT_SCREEN_DPI);

} /* Scale */

static VOID GetMinWindowSize(_In_opt_ HWND hWindow, _Out_opt_ PSIZE pSize)
{
    RECT Bounds = {0};
    UINT uDpi   = 0;

    if (NULL == pSize)
    {
        goto EXIT_GET_MIN_SIZE;
    }

    pSize->cx = 0;
    pSize->cy = 0;

    if (NULL == hWindow)
    {
        goto EXIT_GET_MIN_SIZE;
    }

    uDpi          = GetDpiForWindow(hWindow);
    Bounds.right  = Scale(CLIENT_W, uDpi);
    Bounds.bottom = Scale(CLIENT_H, uDpi);

    /* A failure here only costs a few pixels of frame, so it is ignored. */
    (VOID) AdjustWindowRectExForDpi(&Bounds,
                                    (DWORD)GetWindowLongPtrW(hWindow, GWL_STYLE),
                                    FALSE,
                                    0,
                                    uDpi);

    pSize->cx = Bounds.right - Bounds.left;
    pSize->cy = Bounds.bottom - Bounds.top;

EXIT_GET_MIN_SIZE:

    return;

} /* GetMinWindowSize */

static VOID
PlaceCtl(_In_opt_ HWND hWindow, _In_ INT iCtlId, _In_ INT x, _In_ INT y, _In_ INT cx, _In_ INT cy)
{
    HWND hCtl = NULL;

    if (NULL == hWindow)
    {
        goto EXIT_PLACE_CTL;
    }

    hCtl = GetDlgItem(hWindow, iCtlId);

    if (NULL != hCtl)
    {
        (VOID) MoveWindow(hCtl, x, y, cx, cy, FALSE);
    }

EXIT_PLACE_CTL:

    return;

} /* PlaceCtl */

static VOID LayoutControls(_In_opt_ HWND hWindow)
{
    RECT   Client     = {0};
    RECT   Panel      = {0};
    HWND   hTabs      = NULL;
    UINT   uDpi       = 0;
    SIZE_T iIndex     = 0;
    INT    iStatusTop = 0;
    INT    iIcon      = 0;
    INT    iGap       = 0;
    INT    iLabelW    = 0;
    INT    iRowH      = 0;
    INT    iButtonW   = 0;
    INT    iWidth     = 0;
    INT    x          = 0;
    INT    y          = 0;

    if ((NULL == hWindow) || !GetClientRect(hWindow, &Client))
    {
        goto EXIT_LAYOUT;
    }

    uDpi = GetDpiForWindow(hWindow);

    /* The status block keeps its height along the bottom; the tabs take the
       rest. It sits outside the tab control on purpose, so the connection
       count stays readable while the Help tab is open. */
    iStatusTop   = Client.bottom - Scale(MARGIN + STATUS_H, uDpi);
    Panel.left   = Scale(MARGIN, uDpi);
    Panel.top    = Scale(MARGIN, uDpi);
    Panel.right  = Client.right - Scale(MARGIN, uDpi);
    Panel.bottom = iStatusTop - Scale(ROW_GAP, uDpi);

    PlaceCtl(hWindow,
             IDC_TABS,
             Panel.left,
             Panel.top,
             Panel.right - Panel.left,
             Panel.bottom - Panel.top);
    PlaceCtl(hWindow,
             IDC_STATUS,
             Panel.left,
             iStatusTop,
             Panel.right - Panel.left,
             Scale(STATUS_H, uDpi));

    /* Turns the control rectangle into the usable area inside it, which
       depends on the tab row's font and theme. */
    hTabs = GetDlgItem(hWindow, IDC_TABS);

    if (NULL != hTabs)
    {
        TabCtrl_AdjustRect(hTabs, FALSE, &Panel);
    }

    PlaceCtl(hWindow,
             IDC_HELP_TEXT,
             Panel.left,
             Panel.top,
             Panel.right - Panel.left,
             Panel.bottom - Panel.top);
    PlaceCtl(hWindow,
             IDC_ABOUT_TEXT,
             Panel.left,
             Panel.top,
             Panel.right - Panel.left,
             Panel.bottom - Panel.top);

    x        = Panel.left + Scale(PANEL_PAD, uDpi);
    y        = Panel.top + Scale(PANEL_PAD, uDpi);
    iWidth   = (Panel.right - Panel.left) - Scale(2 * PANEL_PAD, uDpi);
    iIcon    = Scale(ICON_SIZE, uDpi);
    iGap     = Scale(BUTTON_GAP, uDpi);
    iLabelW  = Scale(LABEL_W, uDpi);
    iRowH    = Scale(ROW_H, uDpi);
    iButtonW = Scale(BUTTON_W, uDpi);

    PlaceCtl(hWindow, IDC_SNAIL, x, y, iIcon, iIcon);
    PlaceCtl(hWindow, IDC_TITLE, x + iIcon + iGap, y, iWidth - iIcon - iGap, iIcon);
    y += Scale(HEADER_H, uDpi);

    /* Labels keep their width; the edit boxes take what is left. */
    for (iIndex = 0; iIndex < COUNT_OF(g_aFields); iIndex++)
    {
        PlaceCtl(hWindow, g_aFields[iIndex].iLabelId, x, y, iLabelW, iRowH);
        PlaceCtl(hWindow, g_aFields[iIndex].iCtlId, x + iLabelW, y, iWidth - iLabelW, iRowH);
        y += iRowH + Scale(ROW_GAP, uDpi);
    }

    PlaceCtl(hWindow, IDC_START, x + iLabelW, y, iButtonW, Scale(BUTTON_H, uDpi));
    PlaceCtl(hWindow, IDC_STOP, x + iLabelW + iButtonW + iGap, y, iButtonW, Scale(BUTTON_H, uDpi));

    /* One repaint for the whole move. The transparent labels do not erase
       where they were, so the background has to be redrawn under them. */
    (VOID) RedrawWindow(hWindow, NULL, NULL, RDW_INVALIDATE | RDW_ERASE | RDW_ALLCHILDREN);

EXIT_LAYOUT:

    return;

} /* LayoutControls */

static BOOL CALLBACK ApplyFontToChild(_In_opt_ HWND hChild, _In_ LPARAM lpFont)
{
    if (NULL != hChild)
    {
        SendMessageW(hChild, WM_SETFONT, (WPARAM)lpFont, TRUE);
    }

    /* TRUE regardless, so one bad child does not stop the enumeration. */
    return TRUE;

} /* ApplyFontToChild */

static VOID ApplyUiFont(_In_opt_ HWND hWindow)
{
    NONCLIENTMETRICSW Metrics = {0};
    HFONT             hFont   = NULL;

    if (NULL == hWindow)
    {
        goto EXIT_APPLY_UI_FONT;
    }

    Metrics.cbSize = sizeof(Metrics);

    if (!SystemParametersInfoW(SPI_GETNONCLIENTMETRICS, sizeof(Metrics), &Metrics, 0))
    {
        goto EXIT_APPLY_UI_FONT;
    }

    /* Keep the shell typeface but size it to the layout at this DPI. */
    Metrics.lfMessageFont.lfHeight = Scale(FONT_HEIGHT, GetDpiForWindow(hWindow));

    hFont = CreateFontIndirectW(&Metrics.lfMessageFont);

    if (NULL == hFont)
    {
        goto EXIT_APPLY_UI_FONT;
    }

    /* The old font is deleted only once no control is still using it. */
    EnumChildWindows(hWindow, ApplyFontToChild, (LPARAM)hFont);

    if (NULL != g_hUiFont)
    {
        DeleteObject(g_hUiFont);
    }

    g_hUiFont = hFont;

EXIT_APPLY_UI_FONT:

    return;

} /* ApplyUiFont */

static VOID ApplyTitleFont(_In_opt_ HWND hWindow)
{
    LOGFONTW TitleFont = {0};
    HFONT    hFont     = NULL;

    if (NULL == hWindow)
    {
        goto EXIT_APPLY_TITLE_FONT;
    }

    TitleFont.lfHeight  = Scale(TITLE_FONT_HEIGHT, GetDpiForWindow(hWindow));
    TitleFont.lfWeight  = FW_BOLD;
    TitleFont.lfCharSet = DEFAULT_CHARSET;

    lstrcpynW(TitleFont.lfFaceName, TITLE_FONT_FACE, LF_FACESIZE);

    hFont = CreateFontIndirectW(&TitleFont);

    if (NULL == hFont)
    {
        goto EXIT_APPLY_TITLE_FONT;
    }

    SendDlgItemMessageW(hWindow, IDC_TITLE, WM_SETFONT, (WPARAM)hFont, TRUE);

    if (NULL != g_hTitleFont)
    {
        DeleteObject(g_hTitleFont);
    }

    g_hTitleFont = hFont;

EXIT_APPLY_TITLE_FONT:

    return;

} /* ApplyTitleFont */

static VOID ApplySnailIcon(_In_opt_ HWND hWindow)
{
    HICON hSnail = NULL;
    INT   iSize  = 0;

    if (NULL == hWindow)
    {
        goto EXIT_APPLY_SNAIL;
    }

    iSize = Scale(ICON_SIZE, GetDpiForWindow(hWindow));

    /* Not LR_SHARED: a shared handle keeps the size it was first loaded at,
       and this one changes with the DPI. */
    hSnail = (HICON)LoadImageW((HINSTANCE)GetWindowLongPtrW(hWindow, GWLP_HINSTANCE),
                               MAKEINTRESOURCEW(IDI_SNAIL),
                               IMAGE_ICON,
                               iSize,
                               iSize,
                               LR_DEFAULTCOLOR);

    /* A missing icon leaves a blank space, so it is not fatal. */
    if (NULL == hSnail)
    {
        goto EXIT_APPLY_SNAIL;
    }

    SendDlgItemMessageW(hWindow, IDC_SNAIL, STM_SETICON, (WPARAM)hSnail, 0);

    if (NULL != g_hSnailIcon)
    {
        DestroyIcon(g_hSnailIcon);
    }

    g_hSnailIcon = hSnail;

EXIT_APPLY_SNAIL:

    return;

} /* ApplySnailIcon */

static VOID ApplyDpiResources(_In_opt_ HWND hWindow)
{
    ApplyUiFont(hWindow);
    ApplyTitleFont(hWindow);
    ApplySnailIcon(hWindow);

} /* ApplyDpiResources */

static VOID BuildControls(_In_opt_ HWND hWindow)
{
    SIZE_T iIndex = 0;

    if (NULL == hWindow)
    {
        goto EXIT_BUILD_CONTROLS;
    }

    /* The tab control first, so every control built after it sits above it in
       z order. Reverse the two and the frame paints over the form. */
    AddTabs(hWindow);

    AddCtl(hWindow, 0, WND_CLASS_STATIC, NULL, WS_CHILD | SS_ICON, IDC_SNAIL);
    AddCtl(hWindow, 0, WND_CLASS_STATIC, PROJ_NAME, WS_CHILD | SS_CENTERIMAGE, IDC_TITLE);

    for (iIndex = 0; iIndex < COUNT_OF(g_aFields); iIndex++)
    {
        AddCtl(hWindow,
               0,
               WND_CLASS_STATIC,
               g_aFields[iIndex].pszLabel,
               WS_CHILD | SS_CENTERIMAGE,
               g_aFields[iIndex].iLabelId);
        AddCtl(hWindow,
               WS_EX_CLIENTEDGE,
               WND_CLASS_EDIT,
               g_aFields[iIndex].pszInitial,
               WS_CHILD | WS_TABSTOP | ES_AUTOHSCROLL,
               g_aFields[iIndex].iCtlId);
    }

    AddCtl(hWindow,
           0,
           WND_CLASS_BUTTON,
           CAPTION_START,
           WS_CHILD | WS_TABSTOP | BS_PUSHBUTTON,
           IDC_START);
    AddCtl(hWindow,
           0,
           WND_CLASS_BUTTON,
           CAPTION_STOP,
           WS_CHILD | WS_TABSTOP | BS_PUSHBUTTON,
           IDC_STOP);

    AddCtl(hWindow,
           WS_EX_CLIENTEDGE,
           WND_CLASS_EDIT,
           HelpUsageText(),
           WS_CHILD | WS_TABSTOP | WS_VSCROLL | ES_MULTILINE | ES_READONLY,
           IDC_HELP_TEXT);
    AddCtl(hWindow,
           WS_EX_CLIENTEDGE,
           WND_CLASS_EDIT,
           HelpAboutText(),
           WS_CHILD | WS_TABSTOP | WS_VSCROLL | ES_MULTILINE | ES_READONLY,
           IDC_ABOUT_TEXT);

    /* SS_LEFT and several lines rather than SS_PATHELLIPSIS and one: an
       ellipsis would cut off the session counters at the end. */
    AddCtl(hWindow, 0, WND_CLASS_STATIC, MSG_IDLE, WS_CHILD | WS_VISIBLE | SS_LEFT, IDC_STATUS);

    ApplyDpiResources(hWindow);

EXIT_BUILD_CONTROLS:

    return;

} /* BuildControls */

/* end of file */
