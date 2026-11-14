/**
 * @file Commands.c
 * @brief Start and stop handlers for the relay.
 *
 * Reads the form, validates it, and drives the relay. The window module owns
 * the controls; this module owns what happens when the buttons are pressed
 * and what the status line says afterwards.
 */

#include <WinSock2.h>
#include <Windows.h>
#include <StrSafe.h>

#include "Commands.h"
#include "Help.h"
#include "Relay.h"
#include "Snailnet.h"

/* OnStart copies each address field into RELAY_CONFIG unchecked, which is
   safe only while the form's buffer is no larger than the relay's. */
_Static_assert((INT)ADDR_CCH <= (INT)RELAY_ADDR_CCH, "ADDR_CCH must fit in RELAY_ADDR_CCH");

/** Description of the running relay, rebuilt on every start. */
static WCHAR g_szRunning[STATUS_CCH] = {0};

/** Live sessions, and every session since the last start. */
static UINT g_uActive = 0;
static UINT g_uTotal  = 0;

/**
 * @brief Writes a line of text to the status control.
 *
 * @param hWindow Main window. May be NULL.
 * @param pszText Text to display. May be NULL.
 */
static VOID SetStatus(_In_opt_ HWND hWindow, _In_opt_z_ PCWSTR pszText);

/**
 * @brief Redraws the status line from the running text and the counters.
 *
 * @param hWindow Main window. May be NULL.
 */
static VOID RefreshStatus(_In_opt_ HWND hWindow);

/**
 * @brief Reads and range checks a port field.
 *
 * Reports the problem on the status line when the value is rejected.
 *
 * @param hWindow Main window. May be NULL.
 * @param iCtlId  Edit control to read.
 * @param pszName Field name used in the error text. May be NULL.
 * @param puPort  Receives the port. May be NULL. Meaningful only on success.
 *
 * @return TRUE if the field held a port between PORT_MIN and PORT_MAX.
 */
static BOOL
ReadPort(_In_opt_ HWND hWindow, _In_ INT iCtlId, _In_opt_z_ PCWSTR pszName, _Out_opt_ PUINT puPort);

/**
 * @brief Reads an address field, refusing one too long to hold.
 *
 * GetDlgItemTextW quietly truncates to the buffer, and a cut hostname
 * resolves as a different name, so the length is checked first and an
 * over-long field is reported on the status line instead.
 *
 * @param hWindow Main window.
 * @param iCtlId  Edit control to read.
 * @param pszName Field name used in the error text.
 * @param pszAddr Receives the text. ADDR_CCH characters.
 *
 * @return TRUE if the field fit, FALSE if it was reported as too long.
 */
static BOOL ReadAddress(_In_ HWND                      hWindow,
                        _In_ INT                       iCtlId,
                        _In_z_ PCWSTR                  pszName,
                        _Out_writes_z_(ADDR_CCH) PWSTR pszAddr);

/**
 * @brief Relay notify callback that forwards to the window as WM_RELAY_NOTIFY.
 *
 * @details The whole of this module's binding to Relay.c's generic
 * RELAY_NOTIFY_PROC: it does nothing but post, so it never blocks the
 * relay worker that calls it, and the UI thread remains the only thread
 * that touches a control.
 *
 * @param pContext Main window handle, cast back from PVOID.
 * @param Code     What happened.
 * @param lData    Code specific payload.
 */
static VOID CALLBACK GuiNotifyProc(_In_opt_ PVOID    pContext,
                                   _In_ RELAY_NOTIFY Code,
                                   _In_ LPARAM       lData);

VOID OnStart(_In_opt_ HWND hWindow)
{
    RELAY_CONFIG  Config                     = {0};
    SOCKET_STATUS SockStatus                 = SOCKET_STATUS_ERROR;
    WCHAR         szListenAddr[ADDR_CCH]     = {0};
    WCHAR         szTargetAddr[ADDR_CCH]     = {0};
    WCHAR         szMessage[STATUS_LINE_CCH] = {0};
    UINT          uListenPort                = 0;
    UINT          uTargetPort                = 0;
    UINT          uLatencyMs                 = 0;
    LRESULT       lSelection                 = CB_ERR;
    BOOL          bValid                     = FALSE;
    BOOL          bRead                      = FALSE;

    if (NULL == hWindow)
    {
        goto EXIT_ON_START;
    }

    bRead = ReadAddress(hWindow, IDC_LISTEN_ADDR, LABEL_LISTEN_ADDR, szListenAddr);

    if (!bRead)
    {
        goto EXIT_ON_START;
    }

    bRead = ReadAddress(hWindow, IDC_TARGET_ADDR, LABEL_TARGET_ADDR, szTargetAddr);

    if (!bRead)
    {
        goto EXIT_ON_START;
    }

    if (L'\0' == szListenAddr[0] || L'\0' == szTargetAddr[0])
    {
        SetStatus(hWindow, MSG_NEED_ADDRESSES);
        goto EXIT_ON_START;
    }

    bRead = ReadPort(hWindow, IDC_LISTEN_PORT, LABEL_LISTEN_PORT, &uListenPort);

    if (!bRead)
    {
        goto EXIT_ON_START;
    }

    bRead = ReadPort(hWindow, IDC_TARGET_PORT, LABEL_TARGET_PORT, &uTargetPort);

    if (!bRead)
    {
        goto EXIT_ON_START;
    }

    uLatencyMs = GetDlgItemInt(hWindow, IDC_LATENCY, &bValid, FALSE);

    if (!bValid)
    {
        SetStatus(hWindow, MSG_BAD_LATENCY);
        goto EXIT_ON_START;
    }

    /* Items were added in SNAILNET_DELAY_DIR order, so the index is the
       direction. Nothing selected falls back to the default rather than
       refusing to start. */
    lSelection = SendDlgItemMessageW(hWindow, IDC_DELAY_DIR, CB_GETCURSEL, 0, 0);

    if ((lSelection >= 0) && (lSelection < (LRESULT)DELAY_DIR_COUNT))
    {
        Config.DelayDir = (SNAILNET_DELAY_DIR)lSelection;
    }
    else
    {
        Config.DelayDir = DEFAULT_DELAY_DIR;
    }

    /* The ports go back to text because the resolver takes text. Printing the
       parsed number rather than the raw field also strips whatever spacing
       the user typed. */
    (VOID) StringCchCopyW(Config.wszListenAddr, RELAY_ADDR_CCH, szListenAddr);
    (VOID) StringCchCopyW(Config.wszTargetAddr, RELAY_ADDR_CCH, szTargetAddr);
    (VOID) StringCchPrintfW(Config.wszListenPort, RELAY_PORT_CCH, FMT_PORT_TEXT, uListenPort);
    (VOID) StringCchPrintfW(Config.wszTargetPort, RELAY_PORT_CCH, FMT_PORT_TEXT, uTargetPort);

    Config.dwLatencyMs = (DWORD)uLatencyMs;

    g_uActive = 0;
    g_uTotal  = 0;

    SockStatus = RelayStart(&Config, GuiNotifyProc, (PVOID)hWindow);

    if (SOCKET_IS_ERROR(SockStatus))
    {
        (VOID) StringCchPrintfW(szMessage,
                                STATUS_LINE_CCH,
                                FMT_START_FAILED,
                                SocketStatusText(SockStatus));

        SetStatus(hWindow, szMessage);
        SetRunning(hWindow, FALSE);
        goto EXIT_ON_START;
    }

    (VOID) StringCchPrintfW(g_szRunning,
                            STATUS_CCH,
                            FMT_RUNNING,
                            szListenAddr,
                            uListenPort,
                            szTargetAddr,
                            uTargetPort,
                            uLatencyMs,
                            DelayDirText(Config.DelayDir));

    RefreshStatus(hWindow);
    SetRunning(hWindow, TRUE);

EXIT_ON_START:

    return;

} /* OnStart */

VOID OnStop(_In_opt_ HWND hWindow)
{
    if (NULL == hWindow)
    {
        goto EXIT_ON_STOP;
    }

    /* Painted before the wait, because RelayStop blocks this thread until the
       last session thread is gone and nothing would repaint until then. */
    SetStatus(hWindow, MSG_STOPPING);
    (VOID) UpdateWindow(hWindow);

    RelayStop();

    g_szRunning[0] = L'\0';
    g_uActive      = 0;
    g_uTotal       = 0;

    SetStatus(hWindow, MSG_IDLE);
    SetRunning(hWindow, FALSE);

EXIT_ON_STOP:

    return;

} /* OnStop */

VOID OnShutdown(VOID)
{
    RelayStop();

    return;

} /* OnShutdown */

VOID OnRelayNotify(_In_opt_ HWND hWindow, _In_ WPARAM wParam, _In_ LPARAM lParam)
{
    WCHAR szMessage[STATUS_LINE_CCH] = {0};

    if (NULL == hWindow)
    {
        goto EXIT_ON_NOTIFY;
    }

    switch ((RELAY_NOTIFY)wParam)
    {
    case RELAY_NOTIFY_ACCEPTED:
        g_uActive++;
        g_uTotal++;
        RefreshStatus(hWindow);
        break;

    case RELAY_NOTIFY_CLOSED:
        if (0 < g_uActive)
        {
            g_uActive--;
        }

        RefreshStatus(hWindow);
        break;

    case RELAY_NOTIFY_ERROR:
        (VOID) StringCchPrintfW(szMessage,
                                STATUS_LINE_CCH,
                                FMT_RELAY_ERROR,
                                g_szRunning,
                                SocketStatusText((SOCKET_STATUS)lParam));

        SetStatus(hWindow, szMessage);
        break;

    case RELAY_NOTIFY_LISTENING:
    case RELAY_NOTIFY_STOPPED:
        break;

    default:
        break;
    }

EXIT_ON_NOTIFY:

    return;

} /* OnRelayNotify */

VOID SetRunning(_In_opt_ HWND hWindow, _In_ BOOL bRunning)
{
    HWND hStart = NULL;
    HWND hStop  = NULL;

    if (NULL == hWindow)
    {
        goto EXIT_SET_RUNNING;
    }

    hStart = GetDlgItem(hWindow, IDC_START);
    hStop  = GetDlgItem(hWindow, IDC_STOP);

    /* GetDlgItem returns NULL if the control does not exist yet, which can
       happen if this is called before BuildControls. */
    if (NULL != hStart)
    {
        EnableWindow(hStart, !bRunning);
    }

    if (NULL != hStop)
    {
        EnableWindow(hStop, bRunning);
    }

EXIT_SET_RUNNING:

    return;

} /* SetRunning */

static VOID SetStatus(_In_opt_ HWND hWindow, _In_opt_z_ PCWSTR pszText)
{
    if (NULL == hWindow || NULL == pszText)
    {
        goto EXIT_SET_STATUS;
    }

    SetDlgItemTextW(hWindow, IDC_STATUS, pszText);

EXIT_SET_STATUS:

    return;

} /* SetStatus */

static VOID RefreshStatus(_In_opt_ HWND hWindow)
{
    WCHAR szMessage[STATUS_LINE_CCH] = {0};

    if (NULL == hWindow || L'\0' == g_szRunning[0])
    {
        goto EXIT_REFRESH;
    }

    (VOID) StringCchPrintfW(szMessage,
                            STATUS_LINE_CCH,
                            FMT_SESSIONS,
                            g_szRunning,
                            g_uActive,
                            g_uTotal);

    SetStatus(hWindow, szMessage);

EXIT_REFRESH:

    return;

} /* RefreshStatus */

static BOOL
ReadPort(_In_opt_ HWND hWindow, _In_ INT iCtlId, _In_opt_z_ PCWSTR pszName, _Out_opt_ PUINT puPort)
{
    BOOL  bValid                    = FALSE;
    BOOL  bResult                   = FALSE;
    UINT  uValue                    = 0;
    WCHAR szMessage[PORT_LABEL_CCH] = {0};

    /* Validate before touching puPort; zeroing it first would fault on the
       very input this check exists to reject. */
    if (NULL == hWindow || NULL == pszName || NULL == puPort)
    {
        goto EXIT_READ_PORT;
    }

    *puPort = 0;

    uValue = GetDlgItemInt(hWindow, iCtlId, &bValid, FALSE);

    if (!bValid || uValue < PORT_MIN || uValue > PORT_MAX)
    {
        (VOID)
            StringCchPrintfW(szMessage, PORT_LABEL_CCH, FMT_BAD_PORT, pszName, PORT_MIN, PORT_MAX);

        SetStatus(hWindow, szMessage);
        goto EXIT_READ_PORT;
    }

    *puPort = uValue;
    bResult = TRUE;

EXIT_READ_PORT:

    return bResult;

} /* ReadPort */

static BOOL ReadAddress(_In_ HWND                      hWindow,
                        _In_ INT                       iCtlId,
                        _In_z_ PCWSTR                  pszName,
                        _Out_writes_z_(ADDR_CCH) PWSTR pszAddr)
{
    BOOL  bResult                   = FALSE;
    HWND  hControl                  = NULL;
    INT   cchText                   = 0;
    WCHAR szMessage[PORT_LABEL_CCH] = {0};

    if ((NULL == hWindow) || (NULL == pszName) || (NULL == pszAddr))
    {
        goto EXIT_READ_ADDRESS;
    }

    pszAddr[0] = L'\0';
    hControl   = GetDlgItem(hWindow, iCtlId);

    if (NULL != hControl)
    {
        cchText = GetWindowTextLengthW(hControl);
    }

    /* The length excludes the terminator, so ADDR_CCH - 1 is the most that
       fits. */
    if (cchText >= ADDR_CCH)
    {
        (VOID) StringCchPrintfW(szMessage,
                                PORT_LABEL_CCH,
                                FMT_ADDR_TOO_LONG,
                                pszName,
                                (UINT)(ADDR_CCH - 1));

        SetStatus(hWindow, szMessage);
        goto EXIT_READ_ADDRESS;
    }

    (VOID) GetDlgItemTextW(hWindow, iCtlId, pszAddr, ADDR_CCH);
    bResult = TRUE;

EXIT_READ_ADDRESS:

    return bResult;

} /* ReadAddress */

static VOID CALLBACK GuiNotifyProc(_In_opt_ PVOID    pContext,
                                   _In_ RELAY_NOTIFY Code,
                                   _In_ LPARAM       lData)
{
    HWND hWindow = (HWND)pContext;

    if (NULL == hWindow)
    {
        goto EXIT_GUI_NOTIFY;
    }

    /* Post, never send. A worker that blocks on the UI thread while the UI
       thread is inside RelayStop waiting on that worker is a deadlock. */
    (VOID) PostMessageW(hWindow, WM_RELAY_NOTIFY, (WPARAM)Code, lData);

EXIT_GUI_NOTIFY:

    return;

} /* GuiNotifyProc */

/* end of file */
