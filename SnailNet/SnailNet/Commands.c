/**
 * @file Commands.c
 * @brief Start and stop handlers for the relay.
 *
 * Reads the form, validates it, and drives the relay. The window module owns
 * the controls; this module owns what happens when the buttons are pressed.
 */

#include <Windows.h>
#include <StrSafe.h>

#include "Commands.h"
#include "Snailnet.h"

/**
 * @brief Writes a line of text to the status control.
 *
 * @param hWindow Main window. May be NULL.
 * @param pszText Text to display. May be NULL.
 */
static VOID SetStatus(_In_opt_ HWND hWindow, _In_opt_z_ PCWSTR pszText);

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

VOID OnStart(_In_opt_ HWND hWindow)
{
    WCHAR szListenAddr[ADDR_CCH] = {0};
    WCHAR szTargetAddr[ADDR_CCH] = {0};
    WCHAR szMessage[STATUS_CCH]  = {0};
    UINT  uListenPort            = 0;
    UINT  uTargetPort            = 0;
    UINT  uLatencyMs             = 0;
    BOOL  bValid                 = FALSE;

    if (NULL == hWindow)
    {
        goto EXIT_ON_START;
    }

    GetDlgItemTextW(hWindow, IDC_LISTEN_ADDR, szListenAddr, ADDR_CCH);
    GetDlgItemTextW(hWindow, IDC_TARGET_ADDR, szTargetAddr, ADDR_CCH);

    if (L'\0' == szListenAddr[0] || L'\0' == szTargetAddr[0])
    {
        SetStatus(hWindow, MSG_NEED_ADDRESSES);
        goto EXIT_ON_START;
    }

    if (!ReadPort(hWindow, IDC_LISTEN_PORT, LABEL_LISTEN_PORT, &uListenPort))
    {
        goto EXIT_ON_START;
    }

    if (!ReadPort(hWindow, IDC_TARGET_PORT, LABEL_TARGET_PORT, &uTargetPort))
    {
        goto EXIT_ON_START;
    }

    uLatencyMs = GetDlgItemInt(hWindow, IDC_LATENCY, &bValid, FALSE);

    if (!bValid)
    {
        SetStatus(hWindow, MSG_BAD_LATENCY);
        goto EXIT_ON_START;
    }

    /* TODO: hand the configuration to the relay worker thread. */

    (VOID) StringCchPrintfW(szMessage,
                            STATUS_CCH,
                            FMT_RUNNING,
                            szListenAddr,
                            uListenPort,
                            szTargetAddr,
                            uTargetPort,
                            uLatencyMs);

    SetStatus(hWindow, szMessage);
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

    /* TODO: signal the relay worker thread to shut down and wait for it. */

    SetStatus(hWindow, MSG_IDLE);
    SetRunning(hWindow, FALSE);

EXIT_ON_STOP:

    return;

} /* OnStop */

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

/* end of file */
