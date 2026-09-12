/**
 * @file Commands.h
 * @brief Start and stop handlers for the relay.
 */

#pragma once

#include <Windows.h>

/**
 * @brief Validates the form and starts the relay.
 *
 * @param hWindow Main window.
 */
VOID OnStart(_In_ HWND hWindow);

/**
 * @brief Stops the relay and returns the form to idle.
 *
 * @param hWindow Main window.
 */
VOID OnStop(_In_ HWND hWindow);

/**
 * @brief Enables Start or Stop to match the relay state.
 *
 * Exposed so the window procedure can set the initial state on WM_CREATE.
 *
 * @param hWindow  Main window.
 * @param bRunning TRUE while the relay is active.
 */
VOID SetRunning(_In_ HWND hWindow, _In_ BOOL bRunning);

/* end of file */
