/**
 * @file Main.c
 * @brief Entry point for SnailNet.
 *
 * Brings Winsock up, creates the main window, and runs the message loop.
 * Everything else lives in Ui.c, Commands.c, and Relay.c.
 */

#include <WinSock2.h>
#include <Windows.h>

#include "Relay.h"
#include "Snailnet.h"
#include "Sockets.h"
#include "Ui.h"

INT WINAPI wWinMain(_In_ HINSTANCE     hInstance,
                    _In_opt_ HINSTANCE hPrev,
                    _In_ PWSTR         pszCmdLine,
                    _In_ INT           iShowCommand)
{
    SOCKET_STATUS SockStatus = SOCKET_STATUS_ERROR;
    HWND          hWindow    = NULL;
    MSG           Message    = {0};
    INT           iStatus    = SNAILNET_SUCCESS;

    (VOID) hPrev;
    (VOID) pszCmdLine;

    /* Once for the process. The relay starts and stops many times over a
       session and should not be reference counting Winsock on each pass. */
    SockStatus = SocketLibInit();

    if (SOCKET_IS_ERROR(SockStatus))
    {
        MessageBoxW(NULL, MSG_FAIL_WINSOCK, PROJ_NAME, MB_ICONERROR);
        return SNAILNET_FAIL_WINSOCK;
    }

    hWindow = WindowCreate(hInstance, iShowCommand, &iStatus);

    if (NULL == hWindow)
    {
        SocketLibCleanup();
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
    SocketLibCleanup();

    return (INT)Message.wParam;

} /* wWinMain */

/* end of file */
