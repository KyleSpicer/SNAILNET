/**
 * @file Help.h
 * @brief Static text shown on the Help and About tabs.
 */

#pragma once

#include <Windows.h>

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

/* end of file */
