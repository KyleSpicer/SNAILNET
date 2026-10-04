/**
 * @file Commands.h
 * @brief Start and stop handlers for the relay.
 */

#pragma once

#include <Windows.h>

/**
 * @brief Validates the form and starts the relay.
 *
 * @param hWindow Main window. May be NULL.
 */
VOID OnStart(_In_opt_ HWND hWindow);

/**
 * @brief Stops the relay and returns the form to idle.
 *
 * @param hWindow Main window. May be NULL.
 */
VOID OnStop(_In_opt_ HWND hWindow);

/**
 * @brief Enables Start or Stop to match the relay state.
 *
 * Exposed so the window procedure can set the initial state on WM_CREATE.
 *
 * @param hWindow  Main window. May be NULL.
 * @param bRunning TRUE while the relay is active.
 */
VOID SetRunning(_In_opt_ HWND hWindow, _In_ BOOL bRunning);

/**
 * @brief Tears the relay down during window destruction.
 *
 * Wraps RelayStop so the window module does not have to pull in the socket
 * headers, which would drag winsock into a translation unit that only draws.
 */
VOID OnShutdown(VOID);

/**
 * @brief Folds a worker notification into the status line.
 *
 * Called from the window procedure on WM_RELAY_NOTIFY, so it runs on the UI
 * thread and is the only place allowed to touch the status control.
 *
 * @param hWindow Main window. May be NULL.
 * @param wParam  RELAY_NOTIFY code.
 * @param lParam  Code specific payload.
 */
VOID OnRelayNotify(_In_opt_ HWND hWindow, _In_ WPARAM wParam, _In_ LPARAM lParam);

/* end of file */
