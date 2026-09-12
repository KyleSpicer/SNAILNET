/**
 * @file Ui.h
 * @brief Main window class registration and creation.
 */

#pragma once

#include <Windows.h>

/**
 * @brief Registers the window class, creates the main window, and shows it.
 *
 * @param hInstance    Module instance supplied to wWinMain.
 * @param iShowCommand Show state forwarded to ShowWindow.
 * @param piStatus     Receives a RETURNS code. Written on both paths.
 *
 * @return Window handle, or NULL on failure after reporting to the user.
 */
HWND WindowCreate(_In_ HINSTANCE hInstance, _In_ INT iShowCommand, _Out_ INT *piStatus);

/* end of file */
