<p align="center">
  <img src="Docs/Images/snailnet-color.png" alt="SnailNet" width="400">
</p>

# SnailNet

SnailNet is a lightweight Win32 TCP proxy that injects configurable latency
into a connection. It listens on a local address and port, forwards traffic to
a target address and port, and delays the relayed stream by a user-specified
number of milliseconds. Useful for testing how applications behave over slow
or high-latency links without touching the network stack.

## Table of Contents

- [How it works](#how-it-works)
- [Building from source](#building-from-source)
- [Running SNAILNET](#running-snailnet)
- [Contributing](#contributing)
- [License](#license)

## How it works

TBD

## Building from source

### Prerequisites

- Windows 10 or later, x64
- Visual Studio 2026 with the **Desktop development with C++** workload
  (the C compiler, the Windows SDK, and `rc.exe`)

**Please Note:** No external dependencies. Everything links against the Windows SDK.

### Steps

1. Clone the repository.
2. Open `SnailNet/SnailNet.slnx` in Visual Studio.
3. Set the configuration to **x64** and build (Ctrl+Shift+B).
4. The executable lands in `SnailNet/Build/x64/<Configuration>/SnailNet.exe`.

### Project settings that matter

These are already set in the `.vcxproj`, but they are worth knowing if you
create a new configuration:

| Setting | Value | Why |
|---|---|---|
| Runtime Library | `/MT` (Release), `/MTd` (Debug) | Links the CRT statically so the exe runs without the VC++ redistributable |
| SubSystem | `Windows` | The entry point is `wWinMain`, not `main` |
| Warning Level | `/W4` | Catches indirection and sign mismatches that `/W3` lets through |
| Code Analysis | Enabled | The SAL annotations only do work when `/analyze` runs |

**Please Note:** If you add a static library to the project, it must use the same runtime
setting. Mixing `/MT` and `/MD` produces `LNK2038`, or worse, a build that
links and then corrupts the heap because two CRT copies each think they own
the allocator.

## Running SNAILNET

Launch the executable and fill in five fields:

| Field | Meaning |
|---|---|
| Listen address | Where SnailNet binds. `127.0.0.1` for local testing |
| Listen port | The port your client connects to |
| Target address | The real service |
| Target port | The real service's port |
| Latency (ms) | Delay applied in each direction |

Press **Start**, point your client at the listen address, and traffic flows
through with the delay applied.

## Contributing

Issues and pull requests are welcome.

1. Branch from `main`, naming the branch after the issue: `12-relay-shutdown`.
2. Keep the conventions above. `pre-commit` will catch formatting.
3. Build clean at `/W4` with code analysis on.
4. Reference the issue in the pull request description with `Closes #12`.

Releases are tagged `vMAJOR.MINOR.PATCH`. The project is pre-1.0, so the
public interface is still allowed to change between minor versions.

## License

See [LICENSE](LICENSE).
