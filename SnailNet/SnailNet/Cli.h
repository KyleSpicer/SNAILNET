/**
 * @file Cli.h
 * @brief Headless command line mode.
 *
 * @details Parses argv, starts the relay with the same engine the GUI
 * uses, prints its status and every session or error event to the
 * console as it happens, and stops cleanly on Ctrl+C.
 */

#pragma once

#include <Windows.h>

/**
 * @brief Runs SnailNet headlessly from parsed command line arguments.
 *
 * @details Blocks until Ctrl+C is pressed, the console is closed, or the
 * relay stops on its own, then stops it cleanly before returning.
 *
 * @param argc Argument count, including the program name at argv[0].
 * @param argv Argument vector, including the program name at argv[0].
 *
 * @return SNAILNET_SUCCESS, or a non-zero SNAILNET_STATUS on a bad
 *         argument, a failed start, or the relay stopping unexpectedly.
 */
INT CliRun(_In_ INT argc, _In_reads_(argc) PWSTR argv[]);

/* end of file */
