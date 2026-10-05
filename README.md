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
- [Running it](#running-it)
- [Documentation](#documentation)
- [License](#license)

## How it works

SnailNet sits between a client and a real server. The client connects to
SnailNet's listen address instead of the real one; SnailNet opens its own
connection to the target and forwards bytes between the two.

The delay is **one way**: only traffic heading back to the client is held.
A request reaches the target immediately, and only the reply is delayed, so
a request/response exchange pays the latency once rather than twice.

Delayed bytes are not produced by sleeping on the socket. Each chunk read
from the target is stamped with the moment it becomes due (arrival time
plus the configured latency) and queued. A background pass releases it once
that time passes. Sleeping on the read would add the same delay but also cap
throughput at one buffer per interval; stamping and queuing does not.

Each accepted client gets its own thread and its own connection to the
target, so several clients can be relayed at once without one affecting
another's timing.

## Running it

Launch the executable and fill in five fields:

| Field | Meaning |
|---|---|
| Listen address | Where SnailNet binds. `127.0.0.1` for local testing |
| Listen port | The port your client connects to |
| Target address | The real service |
| Target port | The real service's port |
| Latency (ms) | Delay applied to traffic on its way back to the client |

Press **Start**, point your client at the listen address, and traffic flows
through with the delay applied.

SnailNet also runs headlessly: any command line argument starts the relay
directly, with no window, and `--help` lists them. See
[Docs/UserGuide.md](Docs/UserGuide.md#command-line-mode).

There is no packaged release yet, so for now this means building it
yourself; see [Docs/DeveloperGuide.md](Docs/DeveloperGuide.md). For two
worked examples with diagrams, including one with a client on a different
machine, see [Docs/UserGuide.md](Docs/UserGuide.md).

## Documentation

| Guide | For |
|---|---|
| [Docs/UserGuide.md](Docs/UserGuide.md) | Running SnailNet, worked examples, troubleshooting |
| [Docs/DeveloperGuide.md](Docs/DeveloperGuide.md) | Building from source, project layout, coding conventions, contributing |
| [Docs/TestGuide.md](Docs/TestGuide.md) | Running the automated test suite and reading its output |

## License

See [LICENSE](LICENSE).
