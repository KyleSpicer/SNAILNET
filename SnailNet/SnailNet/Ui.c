/**
 * @file Ui.c
 * @brief Main window: class registration, controls, fonts, window procedure.
 *
 * Owns everything the window is made of. What happens when a button is
 * pressed lives in Commands.c; wWinMain only creates the window and runs the
 * message loop.
 */

#include <Windows.h>

#include "Commands.h"
#include "Resource.h"
#include "Snailnet.h"
#include "Ui.h"

static HFONT g_hTitleFont = NULL;
static HFONT g_uiFont     = NULL;

/**
 * @brief Fills in the class struct and registers it.
 *
 * @param hInstance Module instance that owns the class. May be NULL.
 *
 * @return TRUE if the class registered.
 */
static BOOL RegisterMainClass(_In_opt_ HINSTANCE hInstance);

/**
 * @brief Draws the snail and wordmark across the top of the window.
 *
 * @param hParent   Main window. May be NULL.
 * @param hInstance Module holding the icon resource. May be NULL.
 * @param py        Layout cursor. Advanced past the header. May be NULL.
 */
static VOID AddHeader(_In_opt_ HWND hParent, _In_opt_ HINSTANCE hInstance, _Inout_opt_ PINT py);

/**
 * @brief Adds a label and edit box as one row.
 *
 * @param hParent    Main window. May be NULL.
 * @param pszLabel   Text for the label. May be NULL.
 * @param iCtlId     Control identifier for the edit box.
 * @param pszInitial Starting contents of the edit box. May be NULL.
 * @param py         Layout cursor. Advanced past the row. May be NULL.
 */
static VOID AddField(_In_opt_ HWND     hParent,
                     _In_opt_z_ PCWSTR pszLabel,
                     _In_ INT          iCtlId,
                     _In_opt_z_ PCWSTR pszInitial,
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
    RECT  RectangleBounds = {0, 0, CLIENT_W, CLIENT_H};
    DWORD dwStyle         = WS_OVERLAPPEDWINDOW & ~(WS_THICKFRAME | WS_MAXIMIZEBOX);
    HWND  hWindow         = NULL;
    INT   iStatus         = SNAILNET_SUCCESS;

    if (NULL == hInstance)
    {
        iStatus = SNAILNET_ERROR;
        goto EXIT_CREATE;
    }

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

LRESULT CALLBACK WndProc(_In_ HWND   hWindow,
                         _In_ UINT   Message,
                         _In_ WPARAM wParam,
                         _In_ LPARAM lParam)
{
    switch (Message)
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

    case WM_CTLCOLORSTATIC:
        /* Let labels share the dialog background instead of painting white. */
        SetBkMode((HDC)wParam, TRANSPARENT);
        return (LRESULT)GetSysColorBrush(COLOR_BTNFACE);

    case WM_DESTROY:
        if (NULL != g_hTitleFont)
        {
            DeleteObject(g_hTitleFont);
            g_hTitleFont = NULL;
        }

        if (NULL != g_uiFont)
        {
            DeleteObject(g_uiFont);
            g_uiFont = NULL;
        }

        PostQuitMessage(SNAILNET_SUCCESS);
        return 0;
    }

    return DefWindowProcW(hWindow, Message, wParam, lParam);

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
       scaled system default. A NULL result is acceptable here: Windows falls
       back to the default application icon. */
    WindowClass.hIcon = (HICON)LoadImageW(hInstance,
                                          MAKEINTRESOURCEW(IDI_SNAIL),
                                          IMAGE_ICON,
                                          GetSystemMetrics(SM_CXICON),
                                          GetSystemMetrics(SM_CYICON),
                                          LR_DEFAULTCOLOR);

    WindowClass.hIconSm = (HICON)LoadImageW(hInstance,
                                            MAKEINTRESOURCEW(IDI_SNAIL),
                                            IMAGE_ICON,
                                            GetSystemMetrics(SM_CXSMICON),
                                            GetSystemMetrics(SM_CYSMICON),
                                            LR_DEFAULTCOLOR);

    bResult = (RegisterClassExW(&WindowClass) != 0);

EXIT_REGISTER:

    return bResult;

} /* RegisterMainClass */

static VOID AddHeader(_In_opt_ HWND hParent, _In_opt_ HINSTANCE hInstance, _Inout_opt_ PINT py)
{
    HWND  hIconCtl = NULL;
    HICON hSnail   = NULL;

    if (NULL == hParent || NULL == hInstance || NULL == py)
    {
        goto EXIT_ADD_HEADER;
    }

    /* Request a size present in the .ico so Windows picks that entry rather
       than resampling, which would smear the artwork. */
    hSnail = (HICON)LoadImageW(hInstance,
                               MAKEINTRESOURCEW(IDI_SNAIL),
                               IMAGE_ICON,
                               ICON_SIZE,
                               ICON_SIZE,
                               LR_DEFAULTCOLOR);

    hIconCtl = CreateWindowExW(0,
                               WND_CLASS_STATIC,
                               NULL,
                               WS_CHILD | WS_VISIBLE | SS_ICON,
                               MARGIN,
                               *py,
                               ICON_SIZE,
                               ICON_SIZE,
                               hParent,
                               (HMENU)(INT_PTR)IDC_SNAIL,
                               NULL,
                               NULL);

    /* A missing icon or control leaves a blank space rather than stopping the
       rest of the layout, so neither failure is fatal. */
    if (NULL != hIconCtl && NULL != hSnail)
    {
        SendMessageW(hIconCtl, STM_SETICON, (WPARAM)hSnail, 0);
    }

    CreateWindowExW(0,
                    WND_CLASS_STATIC,
                    PROJ_NAME,
                    WS_CHILD | WS_VISIBLE | SS_CENTERIMAGE,
                    MARGIN + ICON_SIZE + BUTTON_GAP,
                    *py,
                    CLIENT_W - MARGIN - ICON_SIZE - BUTTON_GAP - MARGIN,
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
                     _In_ INT          iCtlId,
                     _In_opt_z_ PCWSTR pszInitial,
                     _Inout_opt_ PINT  py)
{
    if (NULL == hParent || NULL == pszLabel || NULL == pszInitial || NULL == py)
    {
        goto EXIT_ADD_FIELD;
    }

    CreateWindowExW(0,
                    WND_CLASS_STATIC,
                    pszLabel,
                    WS_CHILD | WS_VISIBLE | SS_CENTERIMAGE,
                    MARGIN,
                    *py,
                    LABEL_W,
                    ROW_H,
                    hParent,
                    NULL,
                    NULL,
                    NULL);

    CreateWindowExW(WS_EX_CLIENTEDGE,
                    WND_CLASS_EDIT,
                    pszInitial,
                    WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL,
                    MARGIN + LABEL_W,
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
    if (NULL == hParent || NULL == pszCaption)
    {
        goto EXIT_ADD_BUTTON;
    }

    CreateWindowExW(0,
                    WND_CLASS_BUTTON,
                    pszCaption,
                    WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
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
    if (NULL != g_uiFont)
    {
        DeleteObject(g_uiFont);
        g_uiFont = NULL;
    }

    g_uiFont = CreateFontIndirectW(&Metrics.lfMessageFont);

    if (NULL == g_uiFont)
    {
        goto EXIT_APPLY_UI_FONT;
    }

    EnumChildWindows(hWindow, ApplyFontToChild, (LPARAM)g_uiFont);

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
    INT y = MARGIN;

    if (NULL == hWindow || NULL == hInstance)
    {
        goto EXIT_BUILD_CONTROLS;
    }

    AddHeader(hWindow, hInstance, &y);

    AddField(hWindow, LABEL_LISTEN_ADDR, IDC_LISTEN_ADDR, DEFAULT_LISTEN_ADDR, &y);
    AddField(hWindow, LABEL_LISTEN_PORT, IDC_LISTEN_PORT, DEFAULT_LISTEN_PORT, &y);
    AddField(hWindow, LABEL_TARGET_ADDR, IDC_TARGET_ADDR, DEFAULT_TARGET_ADDR, &y);
    AddField(hWindow, LABEL_TARGET_PORT, IDC_TARGET_PORT, DEFAULT_TARGET_PORT, &y);
    AddField(hWindow, LABEL_LATENCY, IDC_LATENCY, DEFAULT_LATENCY, &y);

    AddButton(hWindow, CAPTION_START, IDC_START, MARGIN + LABEL_W, y);
    AddButton(hWindow, CAPTION_STOP, IDC_STOP, MARGIN + LABEL_W + BUTTON_W + BUTTON_GAP, y);

    y += BUTTON_H + ROW_GAP;

    CreateWindowExW(0,
                    WND_CLASS_STATIC,
                    MSG_IDLE,
                    WS_CHILD | WS_VISIBLE | SS_PATHELLIPSIS,
                    MARGIN,
                    y,
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
