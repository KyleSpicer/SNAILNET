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

static HFONT g_hTitleFont = NULL;
static HFONT g_hUiFont    = NULL;

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
 *
 * @return Tab control handle, or NULL.
 */
static HWND AddTabs(_In_opt_ HWND hParent);

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
 * @brief Draws the snail and wordmark across the top of the panel.
 *
 * @param hParent   Main window. May be NULL.
 * @param hInstance Module holding the icon resource. May be NULL.
 * @param x         Left edge of the panel.
 * @param iWidth    Usable panel width.
 * @param py        Layout cursor. Advanced past the header. May be NULL.
 */
static VOID AddHeader(_In_opt_ HWND      hParent,
                      _In_opt_ HINSTANCE hInstance,
                      _In_ INT           x,
                      _In_ INT           iWidth,
                      _Inout_opt_ PINT   py);

/**
 * @brief Adds a label and edit box as one row.
 *
 * @param hParent    Main window. May be NULL.
 * @param pszLabel   Text for the label. May be NULL.
 * @param iLabelId   Control identifier for the label.
 * @param iCtlId     Control identifier for the edit box.
 * @param pszInitial Starting contents of the edit box. May be NULL.
 * @param x          Left edge of the panel.
 * @param py         Layout cursor. Advanced past the row. May be NULL.
 */
static VOID AddField(_In_opt_ HWND     hParent,
                     _In_opt_z_ PCWSTR pszLabel,
                     _In_ INT          iLabelId,
                     _In_ INT          iCtlId,
                     _In_opt_z_ PCWSTR pszInitial,
                     _In_ INT          x,
                     _Inout_opt_ PINT  py);

/**
 * @brief Adds a push button at an explicit position.
 *
 * @param hParent    Main window. May be NULL.
 * @param pszCaption Button text. May be NULL.
 * @param iCtlId     Control identifier.
 * @param x          Left edge.
 * @param y          Top edge.
 */
static VOID AddButton(
    _In_opt_ HWND hParent, _In_opt_z_ PCWSTR pszCaption, _In_ INT iCtlId, _In_ INT x, _In_ INT y);

/**
 * @brief Adds a read only body of text filling the panel.
 *
 * @param hParent Main window. May be NULL.
 * @param iCtlId  Control identifier.
 * @param pszText Text to display. May be NULL.
 * @param prc     Panel rectangle. May be NULL.
 */
static VOID AddTextPanel(_In_opt_ HWND        hParent,
                         _In_ INT             iCtlId,
                         _In_opt_z_ PCWSTR    pszText,
                         _In_opt_ CONST RECT *prc);

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
 * @brief Creates every child control and applies the fonts.
 *
 * @param hWindow   Main window. May be NULL.
 * @param hInstance Module holding the icon resource. May be NULL.
 */
static VOID BuildControls(_In_opt_ HWND hWindow, _In_opt_ HINSTANCE hInstance);

HWND WindowCreate(_In_opt_ HINSTANCE hInstance, _In_ INT iShowCommand, _Out_opt_ PINT piStatus)
{
    INITCOMMONCONTROLSEX Controls        = {0};
    RECT                 RectangleBounds = {0, 0, CLIENT_W, CLIENT_H};
    DWORD                dwStyle         = WS_OVERLAPPEDWINDOW & ~(WS_THICKFRAME | WS_MAXIMIZEBOX);
    HWND                 hWindow         = NULL;
    INT                  iStatus         = SNAILNET_SUCCESS;

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

    /* Grow the rectangle by the frame so the client area ends up exactly
       CLIENT_W x CLIENT_H. A failure here only costs us a few pixels, so the
       result is deliberately ignored. */
    (VOID) AdjustWindowRect(&RectangleBounds, dwStyle, FALSE);

    hWindow = CreateWindowExW(0,
                              WINDOW_CLASS,
                              PROJ_NAME,
                              dwStyle,
                              CW_USEDEFAULT,
                              CW_USEDEFAULT,
                              RectangleBounds.right - RectangleBounds.left,
                              RectangleBounds.bottom - RectangleBounds.top,
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
    {
        LPCREATESTRUCTW pCreate = (LPCREATESTRUCTW)lParam;

        /* Returning -1 from WM_CREATE aborts window creation, which is the
           only sane response to a create struct we cannot read. */
        if (NULL == pCreate)
        {
            return -1;
        }

        BuildControls(hWindow, pCreate->hInstance);
        SetRunning(hWindow, FALSE);
        ShowTab(hWindow, TAB_RELAY);
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

static HWND AddTabs(_In_opt_ HWND hParent)
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
                            MARGIN,
                            MARGIN,
                            CLIENT_W - 2 * MARGIN,
                            TABS_H,
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

    return hTabs;

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

static VOID AddHeader(_In_opt_ HWND      hParent,
                      _In_opt_ HINSTANCE hInstance,
                      _In_ INT           x,
                      _In_ INT           iWidth,
                      _Inout_opt_ PINT   py)
{
    HWND  hIconCtl = NULL;
    HICON hSnail   = NULL;

    if ((NULL == hParent) || (NULL == hInstance) || (NULL == py))
    {
        goto EXIT_ADD_HEADER;
    }

    /* Request a size present in the .ico so Windows picks that entry rather
       than resampling, which would smear the artwork. LR_SHARED hands the
       static control a handle the system owns, so nothing here has to track
       it for the lifetime of the window. */
    hSnail = (HICON)LoadImageW(hInstance,
                               MAKEINTRESOURCEW(IDI_SNAIL),
                               IMAGE_ICON,
                               ICON_SIZE,
                               ICON_SIZE,
                               LR_DEFAULTCOLOR | LR_SHARED);

    hIconCtl = CreateWindowExW(0,
                               WND_CLASS_STATIC,
                               NULL,
                               WS_CHILD | SS_ICON,
                               x,
                               *py,
                               ICON_SIZE,
                               ICON_SIZE,
                               hParent,
                               (HMENU)(INT_PTR)IDC_SNAIL,
                               NULL,
                               NULL);

    /* A missing icon or control leaves a blank space rather than stopping the
       rest of the layout, so neither failure is fatal. */
    if ((NULL != hIconCtl) && (NULL != hSnail))
    {
        SendMessageW(hIconCtl, STM_SETICON, (WPARAM)hSnail, 0);
    }

    CreateWindowExW(0,
                    WND_CLASS_STATIC,
                    PROJ_NAME,
                    WS_CHILD | SS_CENTERIMAGE,
                    x + ICON_SIZE + BUTTON_GAP,
                    *py,
                    iWidth - ICON_SIZE - BUTTON_GAP,
                    ICON_SIZE,
                    hParent,
                    (HMENU)(INT_PTR)IDC_TITLE,
                    NULL,
                    NULL);

    *py += HEADER_H;

EXIT_ADD_HEADER:

    return;

} /* AddHeader */

static VOID AddField(_In_opt_ HWND     hParent,
                     _In_opt_z_ PCWSTR pszLabel,
                     _In_ INT          iLabelId,
                     _In_ INT          iCtlId,
                     _In_opt_z_ PCWSTR pszInitial,
                     _In_ INT          x,
                     _Inout_opt_ PINT  py)
{
    if ((NULL == hParent) || (NULL == pszLabel) || (NULL == pszInitial) || (NULL == py))
    {
        goto EXIT_ADD_FIELD;
    }

    CreateWindowExW(0,
                    WND_CLASS_STATIC,
                    pszLabel,
                    WS_CHILD | SS_CENTERIMAGE,
                    x,
                    *py,
                    LABEL_W,
                    ROW_H,
                    hParent,
                    (HMENU)(INT_PTR)iLabelId,
                    NULL,
                    NULL);

    CreateWindowExW(WS_EX_CLIENTEDGE,
                    WND_CLASS_EDIT,
                    pszInitial,
                    WS_CHILD | WS_TABSTOP | ES_AUTOHSCROLL,
                    x + LABEL_W,
                    *py,
                    FIELD_W,
                    ROW_H,
                    hParent,
                    (HMENU)(INT_PTR)iCtlId,
                    NULL,
                    NULL);

    *py += ROW_H + ROW_GAP;

EXIT_ADD_FIELD:

    return;

} /* AddField */

static VOID AddButton(
    _In_opt_ HWND hParent, _In_opt_z_ PCWSTR pszCaption, _In_ INT iCtlId, _In_ INT x, _In_ INT y)
{
    if ((NULL == hParent) || (NULL == pszCaption))
    {
        goto EXIT_ADD_BUTTON;
    }

    CreateWindowExW(0,
                    WND_CLASS_BUTTON,
                    pszCaption,
                    WS_CHILD | WS_TABSTOP | BS_PUSHBUTTON,
                    x,
                    y,
                    BUTTON_W,
                    BUTTON_H,
                    hParent,
                    (HMENU)(INT_PTR)iCtlId,
                    NULL,
                    NULL);

EXIT_ADD_BUTTON:

    return;

} /* AddButton */

static VOID AddTextPanel(_In_opt_ HWND        hParent,
                         _In_ INT             iCtlId,
                         _In_opt_z_ PCWSTR    pszText,
                         _In_opt_ CONST RECT *prc)
{
    if ((NULL == hParent) || (NULL == pszText) || (NULL == prc))
    {
        goto EXIT_ADD_TEXT;
    }

    CreateWindowExW(WS_EX_CLIENTEDGE,
                    WND_CLASS_EDIT,
                    pszText,
                    WS_CHILD | WS_TABSTOP | WS_VSCROLL | ES_MULTILINE | ES_READONLY,
                    prc->left,
                    prc->top,
                    prc->right - prc->left,
                    prc->bottom - prc->top,
                    hParent,
                    (HMENU)(INT_PTR)iCtlId,
                    NULL,
                    NULL);

EXIT_ADD_TEXT:

    return;

} /* AddTextPanel */

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

    if (NULL == hWindow)
    {
        goto EXIT_APPLY_UI_FONT;
    }

    Metrics.cbSize = sizeof(Metrics);

    if (!SystemParametersInfoW(SPI_GETNONCLIENTMETRICS, sizeof(Metrics), &Metrics, 0))
    {
        goto EXIT_APPLY_UI_FONT;
    }

    /* Keep the shell typeface but scale it to match the layout. */
    Metrics.lfMessageFont.lfHeight = FONT_HEIGHT;

    /* Guard against a second call leaking the previous handle. */
    if (NULL != g_hUiFont)
    {
        DeleteObject(g_hUiFont);
        g_hUiFont = NULL;
    }

    g_hUiFont = CreateFontIndirectW(&Metrics.lfMessageFont);

    if (NULL == g_hUiFont)
    {
        goto EXIT_APPLY_UI_FONT;
    }

    EnumChildWindows(hWindow, ApplyFontToChild, (LPARAM)g_hUiFont);

EXIT_APPLY_UI_FONT:

    return;

} /* ApplyUiFont */

static VOID ApplyTitleFont(_In_opt_ HWND hWindow)
{
    LOGFONTW TitleFont = {0};
    HWND     hTitle    = NULL;

    if (NULL == hWindow)
    {
        goto EXIT_APPLY_TITLE_FONT;
    }

    hTitle = GetDlgItem(hWindow, IDC_TITLE);

    if (NULL == hTitle)
    {
        goto EXIT_APPLY_TITLE_FONT;
    }

    TitleFont.lfHeight  = TITLE_FONT_HEIGHT;
    TitleFont.lfWeight  = FW_BOLD;
    TitleFont.lfCharSet = DEFAULT_CHARSET;

    lstrcpynW(TitleFont.lfFaceName, TITLE_FONT_FACE, LF_FACESIZE);

    /* Guard against a second call leaking the previous handle. */
    if (NULL != g_hTitleFont)
    {
        DeleteObject(g_hTitleFont);
        g_hTitleFont = NULL;
    }

    g_hTitleFont = CreateFontIndirectW(&TitleFont);

    if (NULL == g_hTitleFont)
    {
        goto EXIT_APPLY_TITLE_FONT;
    }

    SendMessageW(hTitle, WM_SETFONT, (WPARAM)g_hTitleFont, TRUE);

EXIT_APPLY_TITLE_FONT:

    return;

} /* ApplyTitleFont */

static VOID BuildControls(_In_opt_ HWND hWindow, _In_opt_ HINSTANCE hInstance)
{
    RECT Panel  = {0};
    HWND hTabs  = NULL;
    INT  iWidth = 0;
    INT  x      = 0;
    INT  y      = 0;

    if ((NULL == hWindow) || (NULL == hInstance))
    {
        goto EXIT_BUILD_CONTROLS;
    }

    /* The tab control first, so every control built after it sits above it in
       z order. Reverse the two and the frame paints over the form. */
    hTabs = AddTabs(hWindow);

    Panel.left   = MARGIN;
    Panel.top    = MARGIN;
    Panel.right  = MARGIN + CLIENT_W - 2 * MARGIN;
    Panel.bottom = MARGIN + TABS_H;

    /* Turns the control rectangle into the usable area inside it. Hard coding
       the tab row height instead would break on a different DPI or theme. */
    if (NULL != hTabs)
    {
        TabCtrl_AdjustRect(hTabs, FALSE, &Panel);
    }

    x      = Panel.left + PANEL_PAD;
    y      = Panel.top + PANEL_PAD;
    iWidth = (Panel.right - Panel.left) - 2 * PANEL_PAD;

    AddHeader(hWindow, hInstance, x, iWidth, &y);

    AddField(hWindow,
             LABEL_LISTEN_ADDR,
             IDC_LABEL_LISTEN_ADDR,
             IDC_LISTEN_ADDR,
             DEFAULT_LISTEN_ADDR,
             x,
             &y);
    AddField(hWindow,
             LABEL_LISTEN_PORT,
             IDC_LABEL_LISTEN_PORT,
             IDC_LISTEN_PORT,
             DEFAULT_LISTEN_PORT,
             x,
             &y);
    AddField(hWindow,
             LABEL_TARGET_ADDR,
             IDC_LABEL_TARGET_ADDR,
             IDC_TARGET_ADDR,
             DEFAULT_TARGET_ADDR,
             x,
             &y);
    AddField(hWindow,
             LABEL_TARGET_PORT,
             IDC_LABEL_TARGET_PORT,
             IDC_TARGET_PORT,
             DEFAULT_TARGET_PORT,
             x,
             &y);
    AddField(hWindow, LABEL_LATENCY, IDC_LABEL_LATENCY, IDC_LATENCY, DEFAULT_LATENCY, x, &y);

    AddButton(hWindow, CAPTION_START, IDC_START, x + LABEL_W, y);
    AddButton(hWindow, CAPTION_STOP, IDC_STOP, x + LABEL_W + BUTTON_W + BUTTON_GAP, y);

    AddTextPanel(hWindow, IDC_HELP_TEXT, HelpUsageText(), &Panel);
    AddTextPanel(hWindow, IDC_ABOUT_TEXT, HelpAboutText(), &Panel);

    /* Outside the tab control on purpose, so the connection count stays
       readable while the Help tab is open. */
    CreateWindowExW(0,
                    WND_CLASS_STATIC,
                    MSG_IDLE,
                    WS_CHILD | WS_VISIBLE | SS_PATHELLIPSIS,
                    MARGIN,
                    MARGIN + TABS_H + ROW_GAP,
                    CLIENT_W - 2 * MARGIN,
                    ROW_H,
                    hWindow,
                    (HMENU)(INT_PTR)IDC_STATUS,
                    NULL,
                    NULL);

    ApplyUiFont(hWindow);
    ApplyTitleFont(hWindow);

EXIT_BUILD_CONTROLS:

    return;

} /* BuildControls */

/* end of file */
