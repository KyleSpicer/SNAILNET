<p align="center">
  <img src="Images/snailnet-color.png" alt="SnailNet" width="300">
</p>

# SnailNet Developer Guide

Building from source, the project layout, coding conventions, and how to
contribute.

If you just want to run SnailNet, see [UserGuide.md](UserGuide.md) instead.

## Table of Contents

- [Building from source](#building-from-source)
- [Project settings that matter](#project-settings-that-matter)
- [Project layout](#project-layout)
- [Command line mode](#command-line-mode)
- [Coding conventions](#coding-conventions)
- [Testing](#testing)
- [Contributing](#contributing)
- [Releases](#releases)

## Building from source

### Prerequisites

- Windows 10 or later, x64
- Visual Studio 2026 with the **Desktop development with C++** workload
  (the C compiler, the Windows SDK, and `rc.exe`)

No external dependencies. Everything links against the Windows SDK.

### From Visual Studio

1. Clone the repository.
2. Open `SnailNet/SnailNet.slnx` in Visual Studio.
3. Set the configuration to **x64** and build (Ctrl+Shift+B).
4. The executable lands in `SnailNet/Build/x64/<Configuration>/SnailNet.exe`.

### From the command line

```console
cd SnailNet
msbuild SnailNet.slnx /p:Configuration=Release /p:Platform=x64
```

If `msbuild` is not resolving, open a **Developer Command Prompt for VS
2026** (or **Developer PowerShell**) from the Start menu rather than a plain
terminal; that is what puts it on `PATH`.

**The 32-bit build takes `x86`, not `Win32`.** The solution declares its
platform as `x86` even though the individual projects name their own
configurations `Win32`:

```console
msbuild SnailNet.slnx /p:Configuration=Release /p:Platform=x86
```

`/p:Platform=Win32` fails with `MSB4126: The specified solution
configuration "Release|Win32" is invalid`. This asymmetry comes from the
`.slnx`, not from anything you can fix in a project file, so it is worth
knowing rather than working around.

## Project settings that matter

These are already set in the `.vcxproj` files, but are worth knowing if you
add a new configuration or a new project to the solution:

| Setting | Value | Why |
|---|---|---|
| Runtime Library | `/MT` (Release), `/MTd` (Debug) | Links the CRT statically so the exe runs without the VC++ redistributable |
| SubSystem | `Console` | The entry point is `wmain`, not `wWinMain`; see [Command line mode](#command-line-mode) for why |
| Warning Level | `/W4` | Catches indirection and sign mismatches that `/W3` lets through |

**If you add a static library to the project, it must use the same runtime
setting.** Mixing `/MT` and `/MD` produces `LNK2038`, or worse, a build that
links and then corrupts the heap because two CRT copies each think they own
the allocator.

Function signatures throughout the codebase carry SAL annotations
(`_In_`, `_Out_opt_`, `_Inout_`, and so on). They do not currently gate the
build; `/analyze` is not wired into either `.vcxproj`. They still document
ownership and nullability at every call site, which is why they are kept up
to date even without a checker enforcing them.

## Project layout

Two projects. `Sockets` is a small, reusable Winsock wrapper with no
dependency on the GUI; `SnailNet` is the application built on top of it.

| File | What it holds |
|---|---|
| `Sockets/Sockets.c`, `.h` | Event-driven, non-blocking Winsock wrapper: a listener type and a connection type, both built on `WSAEventSelect` |
| `SnailNet/Main.c` | Entry point (`wmain`): brings up Winsock, then dispatches to `CliRun` if given any argument, or the GUI message loop if given none |
| `SnailNet/Cli.c`, `.h` | Headless command line mode: argument parsing, the console notify callback, Ctrl+C handling |
| `SnailNet/Ui.c`, `.h` | Window class registration, control layout, fonts, the window procedure |
| `SnailNet/Commands.c`, `.h` | Start/Stop handlers: validates the form, drives the relay, updates the status line |
| `SnailNet/Relay.c`, `.h` | The relay itself: one accept thread, one session thread per client, the delay queue. Knows nothing about windows or consoles; see [Command line mode](#command-line-mode) |
| `SnailNet/Help.c`, `.h` | Static text for the Help and About tabs |
| `SnailNet/Snailnet.h` | Shared constants: labels, defaults, layout, limits, exit codes |
| `SnailNet/Resource.h`, `.rc` | The window icon resource |

`Tests/` is a separate Poetry-managed Python project; see
[TestGuide.md](TestGuide.md) rather than this guide for anything there.

## Command line mode

With any argument at all, `wmain` dispatches to `CliRun` instead of the
GUI; see [UserGuide.md](UserGuide.md#command-line-mode) for the flags
themselves. Two decisions worth knowing if you touch this:

**The project builds as `Console`, not `Windows`.** A Win32 executable's
subsystem is fixed at link time, so a single binary that is sometimes a
GUI and sometimes a console app has to pick one and compensate for the
other. The alternative, staying `Windows` and calling `AttachConsole` for
the CLI path, avoids the console flash on GUI launch below but pushes
real complexity onto stdio redirection and piping, which matters more for
a tool meant to be scripted. `RunGui` in `Main.c` hides its own console
window (`ShowWindow(GetConsoleWindow(), SW_HIDE)`) immediately on
startup; a brief flash before that runs is the accepted cost.

One consequence: stdout is line buffered against a real console but
fully buffered against a redirected file or pipe, which would hold every
line until the process exits, the opposite of what a piped or logged CLI
tool needs. `CliRun` sets it unbuffered with `setvbuf` before printing
anything, rather than calling `fflush` after every line.

**`Relay.c` notifies through a plain callback, not a window message.**
It used to call `PostMessageW` directly, which hard requires an `HWND`
and so could not serve a console caller. `RelayStart` now takes a
`RELAY_NOTIFY_PROC` callback plus an opaque context pointer instead; the
GUI installs `GuiNotifyProc` in `Commands.c` (a one-line trampoline that
does exactly what the removed code did), and `Cli.c` installs
`CliNotifyProc`, which prints. The callback is called directly from
whichever worker thread has the news, never posted or queued, so it must
not block: that non-blocking requirement is what used to come from
`PostMessageW` being inherently asynchronous, and now has to come from
the callback's own implementation instead. `Sockets.c` already had no
GUI dependency; this makes `Relay.c` consistent with it.

**`GenerateConsoleCtrlEvent` needs the calling process to have a real
console.** This matters if you are scripting SnailNet from something
that redirects or detaches its own standard handles: `GetConsoleWindow()`
returning NULL there means a sent Ctrl+C has nothing to deliver through
and is silently a no-op, not an error. `Tests/tests/test_cli.py` checks
for this (`has_console()`) and skips its Ctrl+C test rather than hanging
on environments without one; the shutdown path itself was confirmed
against a real console during development.

## Coding conventions

`.clang-format` at the repository root defines formatting and runs via
`pre-commit`; do not fight it by hand. A few conventions it does not
enforce, already in use throughout the C sources:

- **Every function has one exit path.** A function with cleanup to do uses
  a single `EXIT_FUNCTION_NAME:` label at the end and `goto` there on every
  early return, rather than returning from the middle. Look at any function
  in `Relay.c` or `Sockets.c` for the pattern.
- **Every public function is documented with a Doxygen `/** @brief */`
  comment** in its header, including parameter directions and what each
  return value means.
- **No hard-coded strings in a `.c` file.** Every user-facing or returned
  string is a named constant defined near the top of the file (or in
  `Snailnet.h`, for anything the GUI modules share). `Sockets.c`'s
  `SocketStatusText` is a good example: sixteen fixed strings, all defined
  together above the function that returns them.
- **SAL annotations on every parameter** (`_In_`, `_Out_opt_`, `_Inout_`,
  and so on), even though `/analyze` is not currently run; see
  [above](#project-settings-that-matter).

## Testing

An automated suite drives the built executable through its window, moving
real TCP traffic through the relay and checking what arrives. See
[TestGuide.md](TestGuide.md) for how to run it, what it covers, and how to
read a failure.

## Contributing

Issues and pull requests are welcome.

1. Branch from `main`, naming the branch after the issue: `12-relay-shutdown`.
2. Keep the conventions above; `pre-commit` will catch formatting.
3. Build clean at `/W4`, for both x64 configurations.
4. Run the test suite before opening the pull request.
5. Reference the issue in the pull request description with `Closes #12`.

## Releases

Releases are tagged `vMAJOR.MINOR.PATCH`. The project is pre-1.0, so the
public interface is still allowed to change between minor versions.

Release packaging (building, zipping, publishing to a Gitea release on tag
push) is not yet automated; that work is tracked as its own project issue.
