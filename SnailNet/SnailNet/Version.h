/* Version.h - bump together with the files listed under Releases in Docs/developerguide.md */

#pragma once

#define SNAILNET_VERSION_MAJOR 1
#define SNAILNET_VERSION_MINOR 0
#define SNAILNET_VERSION_PATCH 0
#define SNAILNET_VERSION_STR "1.0.0"

/* The same version as a wide string, for the UI and the CLI, built from
   SNAILNET_VERSION_STR so there is still only one place to bump it. The
   resource compiler only needs the narrow form, so it never sees this. */
#ifndef RC_INVOKED
#define SNAILNET_WIDEN_INNER(Text) L##Text
#define SNAILNET_WIDEN(Text) SNAILNET_WIDEN_INNER(Text)
#define SNAILNET_VERSION_WSTR SNAILNET_WIDEN(SNAILNET_VERSION_STR)
#endif
