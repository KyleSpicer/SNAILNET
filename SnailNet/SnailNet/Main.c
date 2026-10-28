/**
 * @file Main.c
 * @brief Entry point for SnailNet.
 *
 * SnailNet is a console application: Windows will not let a GUI subsystem
 * executable do ordinary stdio (printf, piping, redirection) without
 * calling AttachConsole and re-opening the standard handles by hand, and
 * that plumbing has real edge cases of its own around redirected and
 * piped output. Being a console application sidesteps all of that for
 * the CLI path, at the cost of a brief console window flash on every
 * launch; see RunGui.
 *
 * With any argument at all, SnailNet runs headlessly; see Cli.c. With
 * none, it launches its GUI; everything that needs lives in Ui.c,
 * Commands.c, and Relay.c.
 */

#include <WinSock2.h>
#include <Windows.h>

#include <stdio.h>

#include "Cli.h"
#include "Relay.h"
#include "Snailnet.h"
#include "Sockets.h"
#include "Ui.h"

/**
 * @brief Runs the GUI: creates the main window and its message loop.
 *
 * @return Process exit code.
 */
static INT RunGui(VOID);

INT wmain(_In_ INT argc, _In_reads_(argc) PWSTR argv[])
{
    SOCKET_STATUS SockStatus = SOCKET_STATUS_ERROR;
    INT           iStatus    = SNAILNET_SUCCESS;
    BOOL          bCliMode   = (1 < argc);

    /* Once for the process. The relay starts and stops many times over a
       session and should not be reference counting Winsock on each pass. */
    SockStatus = SocketLibInit();

    if (SOCKET_IS_ERROR(SockStatus))
    {
        if (bCliMode)
        {
            fwprintf(stderr, L"%s\n", MSG_FAIL_WINSOCK);
        }
        else
        {
            MessageBoxW(NULL, MSG_FAIL_WINSOCK, PROJ_NAME, MB_ICONERROR);
        }

        return SNAILNET_FAIL_WINSOCK;
    }

    if (bCliMode)
    {
        iStatus = CliRun(argc, argv);
    }
    else
    {
        iStatus = RunGui();
    }

    SocketLibCleanup();

    return iStatus;

} /* wmain */

static INT RunGui(VOID)
{
    HWND hWindow = NULL;
    MSG  Message = {0};
    INT  iStatus = SNAILNET_SUCCESS;

    /* A console subsystem binary gets a console on every launch, even one
       headed for the GUI. Hiding it at once makes a launch from Explorer
       look like a plain GUI app after a brief, unavoidable flash. */
    (VOID) ShowWindow(GetConsoleWindow(), SW_HIDE);

    /* Before any window exists. Windows then scales the frame and common
       controls itself, and Ui.c scales the layout, instead of stretching a
       96 DPI bitmap of the window. */
    (VOID) SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);

    hWindow = WindowCreate(GetModuleHandleW(NULL), SW_SHOWDEFAULT, &iStatus);

    if (NULL == hWindow)
    {
        return iStatus;
    }

    while (0 < GetMessageW(&Message, NULL, 0, 0))
    {
        /* IsDialogMessage gives us Tab navigation between the fields. */
        if (!IsDialogMessageW(hWindow, &Message))
        {
            TranslateMessage(&Message);
            DispatchMessageW(&Message);
        }
    }

    /* WM_DESTROY already did this. Repeating it costs nothing and covers the
       paths that leave the loop without going through the window procedure. */
    RelayStop();

    return (INT)Message.wParam;

} /* RunGui */

/* end of file */
