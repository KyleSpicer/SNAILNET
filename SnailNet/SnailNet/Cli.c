/**
 * @file Cli.c
 * @brief Headless command line mode.
 */

/* WinSock2.h has to be pulled in ahead of Windows.h. Windows.h drags in the
   original winsock.h on its own, and the two collide on sockaddr, timeval
   and friends. See Snailnet.h for the same note at the source of it. */
#include <WinSock2.h>
#include <Windows.h>

#include <StrSafe.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <wctype.h>

#include "Cli.h"
#include "Relay.h"
#include "Snailnet.h"
#include "Sockets.h"

/** Bytes in one formatted, timestamped console line. */
#define CLI_LINE_CCH 256

/* Flag names, defined once and reused in both the parser and the usage
   text below, via adjacent wide string literal concatenation, so the two
   cannot drift apart. */
#define CLI_FLAG_LISTEN_ADDR L"--listen-addr"
#define CLI_FLAG_LISTEN_PORT L"--listen-port"
#define CLI_FLAG_TARGET_ADDR L"--target-addr"
#define CLI_FLAG_TARGET_PORT L"--target-port"
#define CLI_FLAG_LATENCY L"--latency"
#define CLI_FLAG_HELP_LONG L"--help"
#define CLI_FLAG_HELP_SHORT L"-h"
#define CLI_FLAG_HELP_QMARK L"-?"

#define FMT_CLI_TIMESTAMPED L"[%02u:%02u:%02u] %s\n"
#define FMT_CLI_SESSION_STARTED L"Session started (%u active, %u total)"
#define FMT_CLI_SESSION_CLOSED L"Session closed (%u active, %u total)"
#define FMT_CLI_ERROR L"Error: %s"
#define FMT_CLI_RUNNING L"Listening on %s:%u, forwarding to %s:%u, latency %u ms\n"
#define FMT_CLI_START_FAILED L"SnailNet: could not start: %s\n"
#define FMT_CLI_BAD_PORT L"SnailNet: %s must be a whole number between %u and %u\n"
#define FMT_CLI_BAD_LATENCY                                                                        \
    L"SnailNet: latency must be a whole, non-negative number of milliseconds\n"
#define FMT_CLI_MISSING_VALUE L"SnailNet: %s needs a value\n"
#define FMT_CLI_UNKNOWN_ARG L"SnailNet: unrecognised argument '%s'\n"
#define FMT_CLI_ADDR_TOO_LONG L"SnailNet: %s must be at most %u characters\n"
#define MSG_CLI_NEED_ADDRESSES L"SnailNet: addresses cannot be blank\n"
#define MSG_CLI_TRY_HELP L"Try 'SnailNet --help'.\n"
#define MSG_CLI_PRESS_CTRL_C L"Press Ctrl+C to stop.\n"
#define MSG_CLI_STOPPING L"\nStopping...\n"
#define MSG_CLI_STOPPED L"Stopped.\n"
#define MSG_CLI_SELF_STOPPED L"SnailNet: the relay stopped unexpectedly.\n"

/**
 * @brief Usage text for --help.
 *
 * @details Plain text rather than built from the CLI_FLAG_* constants
 * below: concatenating them makes clang-format's line wrapping unstable
 * (consecutive formatting passes disagree). The flags are few and change
 * rarely, so keep this text in step with them by hand.
 */
static CONST WCHAR g_szUsage[] =
    L"SnailNet " SNAILNET_VERSION_WSTR L"\n"
    L"\n"
    L"Usage: SnailNet [options]\n"
    L"\n"
    L"Runs the relay headlessly, with no window. With no options at all,\n"
    L"SnailNet launches its GUI instead.\n"
    L"\n"
    L"Options:\n"
    L"  --listen-addr <address>  Local address to bind. Default: 127.0.0.1\n"
    L"  --listen-port <port>     Local port to bind. Default: 8080\n"
    L"  --target-addr <address>  Address to forward to. Default: 127.0.0.1\n"
    L"  --target-port <port>     Port to forward to. Default: 80\n"
    L"  --latency <ms>           Delay applied to traffic on its way back\n"
    L"                           to the client. Default: 250\n"
    L"  --help, -h, -?           Show this text and exit\n"
    L"\n"
    L"Once running, press Ctrl+C to stop.\n"
    L"\n"
    L"Example:\n"
    L"  SnailNet --listen-port 8080 --target-addr 127.0.0.1 --target-port 9000 "
    L"--latency 250\n";

/** What a command line argument turned out to be. */
typedef enum _CLI_OPTION
{
    CLI_OPTION_UNKNOWN = 0,
    CLI_OPTION_HELP,
    CLI_OPTION_LISTEN_ADDR,
    CLI_OPTION_LISTEN_PORT,
    CLI_OPTION_TARGET_ADDR,
    CLI_OPTION_TARGET_PORT,
    CLI_OPTION_LATENCY
} CLI_OPTION,
    *PCLI_OPTION;

/** One recognised flag spelling and the option it selects. */
typedef struct _CLI_FLAG_ENTRY
{
    PCWSTR     pszFlag;
    CLI_OPTION Option;
} CLI_FLAG_ENTRY, *PCLI_FLAG_ENTRY;

/** Every flag the parser accepts. Matched without regard to case. */
static CONST CLI_FLAG_ENTRY g_aCliFlags[] = {
    {CLI_FLAG_HELP_LONG, CLI_OPTION_HELP},
    {CLI_FLAG_HELP_SHORT, CLI_OPTION_HELP},
    {CLI_FLAG_HELP_QMARK, CLI_OPTION_HELP},
    {CLI_FLAG_LISTEN_ADDR, CLI_OPTION_LISTEN_ADDR},
    {CLI_FLAG_LISTEN_PORT, CLI_OPTION_LISTEN_PORT},
    {CLI_FLAG_TARGET_ADDR, CLI_OPTION_TARGET_ADDR},
    {CLI_FLAG_TARGET_PORT, CLI_OPTION_TARGET_PORT},
    {CLI_FLAG_LATENCY, CLI_OPTION_LATENCY},
};

/** Session counters, mirroring Commands.c's g_uActive/g_uTotal for the GUI. */
static UINT g_uCliActive = 0;
static UINT g_uCliTotal  = 0;

/** Set by the console handler so a later RELAY_NOTIFY_STOPPED knows
    whether it is confirming a stop we asked for or reporting one we did
    not; either way, g_hCliStopEvent wakes CliRun's wait exactly once. */
static HANDLE g_hCliStopEvent       = NULL;
static BOOL   g_bCliStoppedBySignal = FALSE;

/**
 * @brief Writes one line to stdout, prefixed with the current time.
 *
 * @param pszFormat printf style format for the line, excluding the prefix.
 */
static VOID PrintTimestamped(_In_z_ PCWSTR pszFormat, ...);

/**
 * @brief Parses a string as an unsigned integer within an inclusive range.
 *
 * @details Stricter than wcstoul: rejects a leading sign, whitespace, or
 * any trailing character wcstoul would otherwise silently ignore, so a
 * typo such as "80a" is reported rather than read as 80.
 *
 * @param pszText Text to parse.
 * @param uMin    Smallest acceptable value, inclusive.
 * @param uMax    Largest acceptable value, inclusive.
 * @param puValue Receives the value. Unchanged on failure.
 *
 * @return TRUE if pszText was entirely digits and fell within range.
 */
static BOOL ParseUInt(_In_z_ PCWSTR pszText, _In_ UINT uMin, _In_ UINT uMax, _Out_ PUINT puValue);

/**
 * @brief Looks an argument up in g_aCliFlags.
 *
 * @param pszArg Argument as typed.
 *
 * @return The option it names, or CLI_OPTION_UNKNOWN.
 */
static CLI_OPTION CliLookupFlag(_In_z_ PCWSTR pszArg);

/**
 * @brief Relay notify callback for command line mode.
 *
 * @details Prints every event to stdout as it arrives, except
 * RELAY_NOTIFY_STOPPED, which wakes CliRun's wait instead; CliRun prints
 * the final line once it knows whether that was expected.
 *
 * @param pContext Unused.
 * @param Code     What happened.
 * @param lData    Code specific payload.
 */
static VOID CALLBACK CliNotifyProc(_In_opt_ PVOID    pContext,
                                   _In_ RELAY_NOTIFY Code,
                                   _In_ LPARAM       lData);

/**
 * @brief Console control handler. Catches Ctrl+C, Ctrl+Break, and the
 * console being closed, and turns any of them into a clean stop.
 *
 * @param dwCtrlType Which signal fired.
 *
 * @return TRUE for a signal handled here, FALSE otherwise.
 */
static BOOL WINAPI CliCtrlHandler(_In_ DWORD dwCtrlType);

INT CliRun(_In_ INT argc, _In_reads_(argc) PWSTR argv[])
{
    RELAY_CONFIG  Config        = {0};
    SOCKET_STATUS SockStatus    = SOCKET_STATUS_ERROR;
    PCWSTR        pszListenAddr = DEFAULT_LISTEN_ADDR;
    PCWSTR        pszTargetAddr = DEFAULT_TARGET_ADDR;
    CLI_OPTION    Option        = CLI_OPTION_UNKNOWN;
    HRESULT       hResult       = S_OK;
    UINT          uListenPort   = 0;
    UINT          uTargetPort   = 0;
    UINT          uLatencyMs    = 0;
    BOOL          bListenOk     = FALSE;
    BOOL          bTargetOk     = FALSE;
    BOOL          bLatencyOk    = FALSE;
    BOOL          bParsed       = FALSE;
    INT           iArg          = 0;
    INT           iResult       = SNAILNET_SUCCESS;

    /* The CRT line buffers stdout against a real console but fully
       buffers it against a redirected file or pipe, which would otherwise
       hold every line here until the process exits -- the opposite of
       what a tool meant to be piped or logged needs. Unbuffered costs
       nothing worth measuring at the rate this ever prints. */
    (VOID) setvbuf(stdout, NULL, _IONBF, 0);

    bListenOk  = ParseUInt(DEFAULT_LISTEN_PORT, PORT_MIN, PORT_MAX, &uListenPort);
    bTargetOk  = ParseUInt(DEFAULT_TARGET_PORT, PORT_MIN, PORT_MAX, &uTargetPort);
    bLatencyOk = ParseUInt(DEFAULT_LATENCY, 0, UINT_MAX, &uLatencyMs);

    if (!bListenOk || !bTargetOk || !bLatencyOk)
    {
        /* The defaults failing to parse would be a bug in Snailnet.h, not
           in anything the caller typed. */
        iResult = SNAILNET_ERROR;
        goto EXIT_CLI_RUN;
    }

    for (iArg = 1; iArg < argc; iArg++)
    {
        Option = CliLookupFlag(argv[iArg]);

        if (CLI_OPTION_HELP == Option)
        {
            wprintf(L"%s", g_szUsage);
            goto EXIT_CLI_RUN;
        }

        if (CLI_OPTION_UNKNOWN == Option)
        {
            fwprintf(stderr, FMT_CLI_UNKNOWN_ARG, argv[iArg]);
            fwprintf(stderr, L"%s", MSG_CLI_TRY_HELP);
            iResult = SNAILNET_ERROR;
            goto EXIT_CLI_RUN;
        }

        /* Every option left takes a value. */
        if ((iArg + 1) >= argc)
        {
            fwprintf(stderr, FMT_CLI_MISSING_VALUE, argv[iArg]);
            iResult = SNAILNET_ERROR;
            goto EXIT_CLI_RUN;
        }

        iArg++;

        switch (Option)
        {
        case CLI_OPTION_LISTEN_ADDR:
            pszListenAddr = argv[iArg];
            break;

        case CLI_OPTION_TARGET_ADDR:
            pszTargetAddr = argv[iArg];
            break;

        case CLI_OPTION_LISTEN_PORT:
            bParsed = ParseUInt(argv[iArg], PORT_MIN, PORT_MAX, &uListenPort);

            if (!bParsed)
            {
                fwprintf(stderr, FMT_CLI_BAD_PORT, CLI_FLAG_LISTEN_PORT, PORT_MIN, PORT_MAX);
                iResult = SNAILNET_ERROR;
                goto EXIT_CLI_RUN;
            }

            break;

        case CLI_OPTION_TARGET_PORT:
            bParsed = ParseUInt(argv[iArg], PORT_MIN, PORT_MAX, &uTargetPort);

            if (!bParsed)
            {
                fwprintf(stderr, FMT_CLI_BAD_PORT, CLI_FLAG_TARGET_PORT, PORT_MIN, PORT_MAX);
                iResult = SNAILNET_ERROR;
                goto EXIT_CLI_RUN;
            }

            break;

        case CLI_OPTION_LATENCY:
            bParsed = ParseUInt(argv[iArg], 0, UINT_MAX, &uLatencyMs);

            if (!bParsed)
            {
                fwprintf(stderr, L"%s", FMT_CLI_BAD_LATENCY);
                iResult = SNAILNET_ERROR;
                goto EXIT_CLI_RUN;
            }

            break;

        default:
            break;
        }
    }

    if ((L'\0' == pszListenAddr[0]) || (L'\0' == pszTargetAddr[0]))
    {
        fwprintf(stderr, L"%s", MSG_CLI_NEED_ADDRESSES);
        iResult = SNAILNET_ERROR;
        goto EXIT_CLI_RUN;
    }

    /* StringCchCopyW truncates to fit and only says so in its result. A cut
       hostname resolves as a different name, so a long address is an error,
       not something to shorten. */
    hResult = StringCchCopyW(Config.wszListenAddr, RELAY_ADDR_CCH, pszListenAddr);

    if (FAILED(hResult))
    {
        fwprintf(stderr, FMT_CLI_ADDR_TOO_LONG, CLI_FLAG_LISTEN_ADDR, RELAY_ADDR_CCH - 1);
        iResult = SNAILNET_ERROR;
        goto EXIT_CLI_RUN;
    }

    hResult = StringCchCopyW(Config.wszTargetAddr, RELAY_ADDR_CCH, pszTargetAddr);

    if (FAILED(hResult))
    {
        fwprintf(stderr, FMT_CLI_ADDR_TOO_LONG, CLI_FLAG_TARGET_ADDR, RELAY_ADDR_CCH - 1);
        iResult = SNAILNET_ERROR;
        goto EXIT_CLI_RUN;
    }

    (VOID) StringCchPrintfW(Config.wszListenPort, RELAY_PORT_CCH, FMT_PORT_TEXT, uListenPort);
    (VOID) StringCchPrintfW(Config.wszTargetPort, RELAY_PORT_CCH, FMT_PORT_TEXT, uTargetPort);
    Config.dwLatencyMs = (DWORD)uLatencyMs;

    /* Manual reset: there is exactly one wait on it, ever, per run. */
    g_hCliStopEvent = CreateEventW(NULL, TRUE, FALSE, NULL);

    if (NULL == g_hCliStopEvent)
    {
        fwprintf(stderr, FMT_CLI_START_FAILED, SocketStatusText(SOCKET_STATUS_CREATE_EVENT_FAIL));
        iResult = SNAILNET_ERROR;
        goto EXIT_CLI_RUN;
    }

    (VOID) SetConsoleCtrlHandler(CliCtrlHandler, TRUE);

    SockStatus = RelayStart(&Config, CliNotifyProc, NULL);

    if (SOCKET_IS_ERROR(SockStatus))
    {
        fwprintf(stderr, FMT_CLI_START_FAILED, SocketStatusText(SockStatus));
        iResult = SNAILNET_ERROR;
        goto EXIT_CLI_RUN;
    }

    wprintf(FMT_CLI_RUNNING, pszListenAddr, uListenPort, pszTargetAddr, uTargetPort, uLatencyMs);
    wprintf(L"%s", MSG_CLI_PRESS_CTRL_C);

    (VOID) WaitForSingleObject(g_hCliStopEvent, INFINITE);

    if (!g_bCliStoppedBySignal)
    {
        wprintf(L"%s", MSG_CLI_SELF_STOPPED);
        iResult = SNAILNET_ERROR;
    }

    /* Safe and idempotent either way: if the relay already stopped on its
       own, the accept thread has already exited and this just joins a
       handle that is already signalled and frees everything else. */
    RelayStop();
    wprintf(L"%s", MSG_CLI_STOPPED);

EXIT_CLI_RUN:

    (VOID) SetConsoleCtrlHandler(CliCtrlHandler, FALSE);

    if (NULL != g_hCliStopEvent)
    {
        (VOID) CloseHandle(g_hCliStopEvent);
        g_hCliStopEvent = NULL;
    }

    return iResult;

} /* CliRun */

static VOID PrintTimestamped(_In_z_ PCWSTR pszFormat, ...)
{
    SYSTEMTIME Time                    = {0};
    WCHAR      szMessage[CLI_LINE_CCH] = {0};
    va_list    Args;

    GetLocalTime(&Time);

    va_start(Args, pszFormat);
    (VOID) StringCchVPrintfW(szMessage, CLI_LINE_CCH, pszFormat, Args);
    va_end(Args);

    wprintf(FMT_CLI_TIMESTAMPED, Time.wHour, Time.wMinute, Time.wSecond, szMessage);

} /* PrintTimestamped */

static BOOL ParseUInt(_In_z_ PCWSTR pszText, _In_ UINT uMin, _In_ UINT uMax, _Out_ PUINT puValue)
{
    ULONGLONG ullValue = 0;
    SIZE_T    cch      = 0;
    SIZE_T    iIndex   = 0;
    INT       iDigit   = 0;
    BOOL      bResult  = FALSE;

    *puValue = 0;

    if (NULL == pszText)
    {
        goto EXIT_PARSE_UINT;
    }

    cch = wcslen(pszText);

    if (0 == cch)
    {
        goto EXIT_PARSE_UINT;
    }

    /* wcstoul and friends accept a leading sign and silently ignore
       trailing garbage; neither is acceptable here, so every character is
       checked by hand before any conversion runs. */
    for (iIndex = 0; iIndex < cch; iIndex++)
    {
        iDigit = iswdigit(pszText[iIndex]);

        if (0 == iDigit)
        {
            goto EXIT_PARSE_UINT;
        }
    }

    ullValue = _wcstoui64(pszText, NULL, 10);

    if ((ullValue < uMin) || (ullValue > uMax))
    {
        goto EXIT_PARSE_UINT;
    }

    *puValue = (UINT)ullValue;
    bResult  = TRUE;

EXIT_PARSE_UINT:

    return bResult;

} /* ParseUInt */

static CLI_OPTION CliLookupFlag(_In_z_ PCWSTR pszArg)
{
    CLI_OPTION Option   = CLI_OPTION_UNKNOWN;
    SIZE_T     iIndex   = 0;
    INT        iCompare = 0;

    if (NULL == pszArg)
    {
        goto EXIT_LOOKUP_FLAG;
    }

    for (iIndex = 0; iIndex < (sizeof(g_aCliFlags) / sizeof(g_aCliFlags[0])); iIndex++)
    {
        iCompare = _wcsicmp(pszArg, g_aCliFlags[iIndex].pszFlag);

        if (0 == iCompare)
        {
            Option = g_aCliFlags[iIndex].Option;
            break;
        }
    }

EXIT_LOOKUP_FLAG:

    return Option;

} /* CliLookupFlag */

static VOID CALLBACK CliNotifyProc(_In_opt_ PVOID    pContext,
                                   _In_ RELAY_NOTIFY Code,
                                   _In_ LPARAM       lData)
{
    (VOID) pContext;

    switch (Code)
    {
    case RELAY_NOTIFY_ACCEPTED:
        g_uCliActive++;
        g_uCliTotal++;
        PrintTimestamped(FMT_CLI_SESSION_STARTED, g_uCliActive, g_uCliTotal);
        break;

    case RELAY_NOTIFY_CLOSED:
        if (0 < g_uCliActive)
        {
            g_uCliActive--;
        }

        PrintTimestamped(FMT_CLI_SESSION_CLOSED, g_uCliActive, g_uCliTotal);
        break;

    case RELAY_NOTIFY_ERROR:
        PrintTimestamped(FMT_CLI_ERROR, SocketStatusText((SOCKET_STATUS)lData));
        break;

    case RELAY_NOTIFY_STOPPED:
        /* Only when this was not already on its way for some other reason:
           SetEvent on an already signalled manual reset event is harmless,
           but the flag below still needs to reflect whichever happened
           first, not whichever Notify call happened to run last. */
        if (!g_bCliStoppedBySignal)
        {
            (VOID) SetEvent(g_hCliStopEvent);
        }

        break;

    case RELAY_NOTIFY_LISTENING:
    default:
        break;
    }

} /* CliNotifyProc */

static BOOL WINAPI CliCtrlHandler(_In_ DWORD dwCtrlType)
{
    BOOL bResult = FALSE;

    switch (dwCtrlType)
    {
    case CTRL_C_EVENT:
    case CTRL_BREAK_EVENT:
    case CTRL_CLOSE_EVENT:
        /* CTRL_CLOSE_EVENT (the console window's own close button) gives
           Windows a short, non-configurable grace period before it kills
           the process regardless of what this handler does. A drain that
           is still waiting on a slow peer when that grace period expires
           is cut short either way; Ctrl+C and Ctrl+Break do not carry that
           risk; see developerguide.md. */
        g_bCliStoppedBySignal = TRUE;
        wprintf(L"%s", MSG_CLI_STOPPING);
        (VOID) SetEvent(g_hCliStopEvent);
        bResult = TRUE;
        break;

    default:
        bResult = FALSE;
        break;
    }

    return bResult;

} /* CliCtrlHandler */

/* end of file */
