<p align="center">
  <img src="image.png" alt="SnailNet" width="300">
</p>

# SnailNet Developer Guide

Building from source, the project layout, coding conventions, and how to
contribute.

If you just want to run SnailNet, see [userguide.md](userguide.md) instead.

## Table of Contents

- [Building from source](#building-from-source)
- [Project settings that matter](#project-settings-that-matter)
- [Project layout](#project-layout)
- [How the relay delays traffic](#how-the-relay-delays-traffic)
- [Command line mode](#command-line-mode)
- [Coding conventions](#coding-conventions)
- [Testing](#testing)
- [Contributing](#contributing)
- [Releases](#releases)

## Building from source

Requirements and build commands are in the README's
[Requirements](../README.md#requirements) and
[Quick start](../README.md#quick-start). One thing the command line build
trips over:

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
| `Sockets/SocketsConstants.h` | Every define the library uses: Winsock settings, wait slots, status text, and the public helper macros. All prefixed `SOCKET_` or `SOCKTXT_`, since `Sockets.h` exposes them to callers |
| `SnailNet/Main.c` | Entry point (`wmain`): brings up Winsock, then dispatches to `CliRun` if given any argument, or the GUI message loop if given none |
| `SnailNet/Cli.c`, `.h` | Headless command line mode: argument parsing, the console notify callback, Ctrl+C handling |
| `SnailNet/Ui.c`, `.h` | Window class registration, control layout, fonts, the window procedure |
| `SnailNet/Commands.c`, `.h` | Start/Stop handlers: validates the form, drives the relay, updates the status line |
| `SnailNet/Relay.c`, `.h` | The relay itself: one accept thread, one session thread per client, the delay queue. Knows nothing about windows or consoles; see [Command line mode](#command-line-mode) |
| `SnailNet/Help.c`, `.h` | Static text for the Help and About tabs |
| `SnailNet/Snailnet.h` | Shared constants: labels, defaults, layout, limits, exit codes |
| `SnailNet/Resource.h`, `.rc` | The window icon resource |

`Tests/` is a separate Poetry-managed Python project; see
[testguide.md](testguide.md) rather than this guide for anything there.

## How the relay delays traffic

The user-visible behavior (one-way delay, one session per client) is
described in [userguide.md](userguide.md#how-it-works). The part that only
matters inside `Relay.c`: delayed bytes are not produced by sleeping on the
socket. Each chunk read from the target is stamped with the moment it
becomes due (arrival time plus the configured latency) and queued, and a
background pass releases it once that time passes. Sleeping on the read
would add the same delay but also cap throughput at one buffer per
interval; stamping and queuing does not.

The queue is capped at `RELAY_QUEUE_MAX_CB` (1 MB) per direction, after
which the relay stops reading from that side until the queue drains. That
cap is what bounds throughput at roughly 1 MB per latency interval, which
the User Guide documents as a limit.

## Command line mode

With any argument at all, `wmain` dispatches to `CliRun` instead of the
GUI; see [userguide.md](userguide.md#command-line-mode) for the flags
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
`RelayStart` takes a `RELAY_NOTIFY_PROC` callback and an opaque context
pointer, so the relay needs no `HWND` and serves both front ends: the GUI
installs `GuiNotifyProc` in `Commands.c`, which posts `WM_RELAY_NOTIFY` to
the window, and `Cli.c` installs `CliNotifyProc`, which prints. The
callback runs directly on whichever worker thread has the news, never
posted or queued, so it must not block. Like `Sockets.c`, `Relay.c` has no
GUI dependency.

**`GenerateConsoleCtrlEvent` needs the calling process to have a real
console.** This matters if you are scripting SnailNet from something
that redirects or detaches its own standard handles: `GetConsoleWindow()`
returning NULL there means a sent Ctrl+C has nothing to deliver through
and is silently a no-op, not an error. A second trap: a process started
with `CREATE_NEW_PROCESS_GROUP` ignores Ctrl+C entirely, and
`GenerateConsoleCtrlEvent` cannot aim Ctrl+C at one group anyway. A script
that needs to stop one SnailNet among others should send Ctrl+Break to
its group, which `CliCtrlHandler` treats the same way. `Tests/tests/test_cli.py`
does exactly that, and checks `has_console()` to skip rather than hang where
there is no console.

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
[testguide.md](testguide.md) for how to run it, what it covers, and how to
read a failure.

## Contributing

Issues and pull requests are welcome.

1. Branch from `main`, naming the branch after the issue: `12-relay-shutdown`.
2. Keep the conventions above; `pre-commit` will catch formatting.
3. Build clean at `/W4`, for both x64 configurations.
4. Run the test suite before opening the pull request.
5. Reference the issue in the pull request description with `Closes #12`.

## Releases

Releases are tagged `vMAJOR.MINOR.PATCH`. From v1.0.0 on, the command line
flags, their defaults, and the exit codes are public interface: changing any
of them is a major version bump.

When cutting a release, bump the version in all of these together:

- `README.md` (the heading)
- `Docs/userguide.md` (the `Version` line under the title)
- `Tests/pyproject.toml` and `Tests/docs/conf.py` (`release`)

### What the release package contains

| File | Source |
|---|---|
| `SnailNet.exe` | x64 Release build |
| `userguide.pdf` | Rendered from `Docs/userguide.md` |
| `cli-report.html` | Command line test report from the same run |

The CI `release` job attaches all three to the Gitea Release for the tag.

The User Guide is the only guide that ships. It is written to stand on
its own as a PDF, so keep it free of links to the other guides or into the
source tree, and keep developer and test detail out of it.

### Rendering the User Guide

CI renders it with the `userguide-pdf` action from the GitOps repository,
which needs only Python. To preview the PDF before pushing, run the same
script from a GitOps checkout, at the root of this repository:

```console
python ../GitOps/actions/userguide-pdf/render_userguide.py
```

It writes `userguide.pdf` at the repository root (gitignored). The first run
installs its pinned renderer into a private environment in your temp folder,
which takes a few seconds; later runs reuse it.

### CI

`.gitea/workflows/build-release.yml` runs on pushes and pull requests to
`main` and `rc*` branches and on `v*` tags, in four jobs:

| Job | Does | Publishes |
|---|---|---|
| `build` | Builds x64 Release | `snailnet-windows-x64` artifact (the exe) |
| `test` | Runs `tests/test_cli.py` against that exe | `cli-test-report` artifact, even when a test fails |
| `docs` | Renders `Docs/userguide.md`, alongside `build` | `userguide` artifact (the PDF) |
| `release` | `v*` tags only, and only if every other job passed | A Gitea Release with the exe, PDF, and report attached |

Artifacts expire after 7 days; release assets do not. Only the command line
tests run in CI, because the runner is a Windows service with no desktop for
the GUI tests' windows.

To cut a release, push a tag: `git tag v1.0.0` then `git push origin v1.0.0`.
The release uses the workflow's automatic token. If Gitea rejects it, add a
personal access token with repository write access as a secret and pass it to
the release step's `token` input.
