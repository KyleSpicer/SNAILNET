/**
 * @file Help.c
 * @brief Static text shown on the Help and About tabs.
 */

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#include <Windows.h>

#include "Help.h"
#include "Snailnet.h"

/**
 * @brief Body text for the usage dialog.
 *
 * @details Edit controls break lines on CRLF, not LF. A lone newline here
 * renders as one unbroken paragraph with a box glyph at each join.
 */
static CONST WCHAR g_szUsage[] =
    L"WHAT IT DOES\r\n"
    L"\r\n"
    L"SnailNet sits between a client and a server and holds traffic back by a "
    L"fixed number of milliseconds. A client connects to the listen address "
    L"instead of the real server, and SnailNet forwards everything on to the "
    L"target address.\r\n"
    L"\r\n"
    L"The delay is one way. Data travelling back to the client is held; data "
    L"heading out to the target is forwarded as soon as it arrives. A "
    L"request and response exchange therefore pays the latency once.\r\n"
    L"\r\n"
    L"TCP only. Each client gets its own connection to the target, and many "
    L"clients can be relayed at once.\r\n"
    L"\r\n"
    L"HOW TO USE IT\r\n"
    L"\r\n"
    L"1. Set the listen address and port. This is what your client connects "
    L"to. Use 127.0.0.1 to keep it on this machine.\r\n"
    L"2. Set the target address and port. This is the real server.\r\n"
    L"3. Set the latency in milliseconds.\r\n"
    L"4. Press Start, then point your client at the listen address.\r\n"
    L"\r\n"
    L"The status line below the tabs shows the active connection count while "
    L"it runs, and stays visible from any tab. Stop closes the listener and "
    L"drops anything still in flight.\r\n"
    L"\r\n"
    L"EXAMPLES\r\n"
    L"\r\n"
    L"These use ncat, from the Nmap package. Run the redirection examples "
    L"from cmd rather than PowerShell, which does not support the input "
    L"redirect and rewrites bytes on the output one.\r\n"
    L"\r\n"
    L"See the delay for yourself:\r\n"
    L"    Listen 127.0.0.1:8080, target 127.0.0.1:9000, latency 2000\r\n"
    L"    Server:  ncat -l 9000 --keep-open\r\n"
    L"    Client:  ncat 127.0.0.1 8080\r\n"
    L"    Typing in the client reaches the server at once. Typing in the "
    L"server takes two seconds to come back. That is the one way delay.\r\n"
    L"\r\n"
    L"Move a file through it:\r\n"
    L"    Listen 127.0.0.1:8080, target 127.0.0.1:9000, latency 250\r\n"
    L"    Server:  ncat -l 9000 --send-only < big.bin\r\n"
    L"    Client:  ncat 127.0.0.1 8080 > out.bin\r\n"
    L"    Compare the two file sizes afterwards. They should match.\r\n"
    L"\r\n"
    L"Find where a client gives up:\r\n"
    L"    Point the client at the listen address and raise the latency "
    L"between runs until it stops waiting. That value is roughly where its "
    L"timeout sits.\r\n"
    L"\r\n"
    L"Watch several connections at once:\r\n"
    L"    Server:  ncat -l 9000 --keep-open\r\n"
    L"    Open a handful of clients against 8080 and watch the active count "
    L"on the status line rise and fall as they come and go.\r\n"
    L"\r\n"
    L"NOTES\r\n"
    L"\r\n"
    L"Held data is capped at one megabyte per connection, so a long delay "
    L"also limits throughput. At 250 ms that works out near four megabytes "
    L"per second.\r\n"
    L"\r\n"
    L"The target address is checked when you press Start, but only that it "
    L"resolves. A host that resolves and is not listening still fails when "
    L"the first client connects.";

/** @brief Body text for the About tab. */
static CONST WCHAR g_szAbout[] =
    L"SnailNet\r\n"
    L"\r\n"
    L"A TCP relay that adds a fixed, configurable delay to traffic on its way "
    L"back to the client. Built for testing how software behaves when the "
    L"network is slower than the developer's desk.\r\n"
    L"\r\n"
    L"Written in C against Winsock2, using event driven non blocking sockets. "
    L"Each client connection is relayed on its own thread, with delayed data "
    L"held in a queue rather than moved by sleeping, so latency shifts when "
    L"bytes arrive without throttling how fast they can flow.\r\n"
    L"\r\n"
    L"See the Help tab for usage and examples.";

PCWSTR HelpUsageText(VOID) { return g_szUsage; } /* HelpUsageText */

PCWSTR HelpAboutText(VOID) { return g_szAbout; } /* HelpAboutText */

/* end of file */
