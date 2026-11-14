/**
 * @file Help.h
 * @brief Static text shown on the Help and About tabs.
 */

#pragma once

#include <Windows.h>

#include "Snailnet.h"

/**
 * @brief Returns the body text for the Help tab.
 *
 * @return A static string. Never NULL.
 */
PCWSTR HelpUsageText(VOID);

/**
 * @brief Returns the body text for the About tab.
 *
 * @return A static string. Never NULL.
 */
PCWSTR HelpAboutText(VOID);

/**
 * @brief Describes a delay direction for a status line.
 *
 * @param DelayDir Direction to describe.
 *
 * @return Static text such as "to client". Never NULL; a value outside
 *         SNAILNET_DELAY_DIR reads as the default.
 */
PCWSTR DelayDirText(_In_ SNAILNET_DELAY_DIR DelayDir);

/* end of file */
