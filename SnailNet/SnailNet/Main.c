/**
 * @file Main.c
 * @brief Entry point for SnailNet.
 *
 * Creates the main window and runs the message loop. Everything else lives in
 * Ui.c and Commands.c.
 */

#include <Windows.h>

#include "Snailnet.h"
#include "Ui.h"

INT WINAPI wWinMain(_In_ HINSTANCE     hInstance,
                    _In_opt_ HINSTANCE hPrev,
                    _In_ PWSTR         pszCmdLine,
                    _In_ INT           iShowCommand)
{
    HWND hWindow = NULL;
    MSG  Message = {0};
    INT  iStatus = SNAILNET_SUCCESS;

    (VOID) hPrev;
    (VOID) pszCmdLine;

    hWindow = WindowCreate(hInstance, iShowCommand, &iStatus);
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

    return (INT)Message.wParam;

} /* wWinMain */

/* end of file */
