/**
 * @file Ui.h
 * @brief Main window class registration and creation.
 */

#pragma once

#include <Windows.h>

/**
 * @brief Registers the window class, creates the main window, and shows it.
 *
 * @param hInstance    Module instance supplied to wWinMain. May be NULL.
 * @param iShowCommand Show state forwarded to ShowWindow.
 * @param piStatus     Receives a SNAILNET_STATUS code. Written on both paths.
 *                     May be NULL.
 *
 * @return Window handle, or NULL on failure after reporting to the user.
 */
HWND WindowCreate(_In_opt_ HINSTANCE hInstance, _In_ INT iShowCommand, _Out_opt_ PINT piStatus);

/* end of file */
