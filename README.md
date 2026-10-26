<p align="center">
  <img src="Docs/image.png" alt="SnailNet" width="400">
</p>

# SnailNet v1.0.0

SnailNet is a lightweight Win32 TCP proxy that injects configurable latency
into a connection, for testing how software behaves over slow or
high-latency links. It is written in C against Winsock2, with a small GUI
and a headless command line mode in the same executable.

This README is for developers working on SnailNet. **If you just want to
use it, see the [User Guide](Docs/userguide.md)**, which ships as a PDF
alongside `SnailNet.exe` in every release.

## Table of Contents

- [Requirements](#requirements)
- [Quick start](#quick-start)
- [Repository layout](#repository-layout)
- [Documentation](#documentation)
- [Contributing](#contributing)
- [License](#license)

## Requirements

| | Required | Notes |
|---|---|---|
| OS | Windows 10 or later, x64 | Win32 and Winsock only; does not build or run on Linux or macOS |
| IDE | Visual Studio 2026 (toolset `v145`) | With the **Desktop development with C++** workload: the C compiler, Windows SDK, and `rc.exe` |
| Language | C | Warning level `/W4`, CRT linked statically (`/MT`) |
| Dependencies | None | Links only against the Windows SDK |

To run the test suite and the formatting hooks, you also need:

| | Required | Notes |
|---|---|---|
| Python | 3.11 or newer | Runs the end-to-end test suite in `Tests/` |
| [Poetry](https://python-poetry.org/docs/#installation) | 2.x | Manages the test suite's virtual environment |
| [pre-commit](https://pre-commit.com/) | Any recent | Runs `clang-format`, `black`, `flake8`, `pylint`, and `isort` on commit |

## Quick start

```console
git clone https://gitea.com/4-Projects/SNAILNET.git
cd SNAILNET
pre-commit install

cd SnailNet
msbuild SnailNet.slnx /p:Configuration=Release /p:Platform=x64

cd ..\Tests
poetry install
poetry run pytest
```

The executable lands in `SnailNet/Build/x64/Release/SnailNet.exe`. You can
also open `SnailNet/SnailNet.slnx` in Visual Studio and build with
Ctrl+Shift+B.

If `msbuild` is not found, use a **Developer PowerShell for VS 2026** rather
than a plain terminal. The 32-bit build takes `/p:Platform=x86`, not
`Win32`; the [Developer Guide](Docs/developerguide.md#building-from-source)
explains why.

## Repository layout

| Path | What it holds |
|---|---|
| `SnailNet/SnailNet.slnx` | The Visual Studio solution |
| `SnailNet/Sockets/` | A small, reusable, event-driven Winsock wrapper with no GUI dependency |
| `SnailNet/SnailNet/` | The application: entry point, GUI, CLI, and the relay itself |
| `Tests/` | End-to-end test suite (Python, Poetry) that drives the built executable |
| `Docs/` | The user, developer, and test guides, plus images |
| `.gitea/workflows/build-release.yml` | CI: builds, runs the command line tests, and on `v*` tags publishes a release |

There is a file-by-file breakdown in the
[Developer Guide](Docs/developerguide.md#project-layout).

## Documentation

| Guide | Audience | Covers |
|---|---|---|
| [Docs/userguide.md](Docs/userguide.md) | Anyone running SnailNet | The GUI, command line flags, worked examples, troubleshooting. Rendered to PDF for the release package |
| [Docs/developerguide.md](Docs/developerguide.md) | Contributors | Building, project settings, architecture, coding conventions, releases |
| [Docs/testguide.md](Docs/testguide.md) | Contributors | Running the test suite, reading its output, what each test proves |

Behavior that users can see, such as flags, defaults, and examples, is
documented once in the User Guide. The other guides link to it rather than
repeating it.

## Contributing

Branch from `main`, naming the branch after the issue (`12-relay-shutdown`).
Build clean at `/W4`, run the test suite, and reference the issue in the pull
request with `Closes #12`. The
[Developer Guide](Docs/developerguide.md#contributing) has the full
checklist and the coding conventions.

## License

See [LICENSE](LICENSE).
