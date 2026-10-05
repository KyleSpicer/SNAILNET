<p align="center">
  <img src="Images/snailnet-color.png" alt="SnailNet" width="300">
</p>

# SnailNet User Guide

How to run SnailNet and see its delay working, with two worked examples.

This guide is for using the built application. If you want to build it from
source or contribute code, see [DeveloperGuide.md](DeveloperGuide.md).

## Table of Contents

- [Getting SnailNet](#getting-snailnet)
- [How it works](#how-it-works)
- [Running it](#running-it)
- [Command line mode](#command-line-mode)
- [Example: local loopback with ncat](#example-local-loopback-with-ncat)
- [Example: remote client, Ubuntu to a Windows target](#example-remote-client-ubuntu-to-a-windows-target)
- [Troubleshooting](#troubleshooting)

## Getting SnailNet

There is no packaged release yet (tracked in the project's Package Release
issue), so for now you build it yourself. See
[DeveloperGuide.md](DeveloperGuide.md#building-from-source) for the steps;
building itself needs nothing beyond Visual Studio, and the result is one
file, `SnailNet.exe`, with no installer and nothing else to carry with it.

## How it works

SnailNet sits between a client and a real server. The client connects to
SnailNet's listen address instead of the real one; SnailNet opens its own
connection to the target and forwards bytes between the two.

The delay is **one way**: only traffic heading back to the client is held.
A request reaches the target immediately, and only the reply is delayed, so
a request/response exchange pays the latency once rather than twice.

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
through with the delay applied. Press **Stop** to close the listener; this
drops anything still in flight.

The status line below the fields shows the active and total session counts
while the relay runs, and reports the reason plainly if something goes
wrong, for example a target that refused the connection.

The two examples below use [ncat](https://nmap.org/ncat/) (installed with
Nmap) to stand in for a client and a server, so you can see the delay
without needing a real application on hand.

## Command line mode

Running `SnailNet.exe` with any argument at all starts the relay directly,
with no window, rather than launching the GUI. This is the same engine and
the same validation; only the interface differs.

```console
SnailNet.exe --listen-port 8080 --target-addr 127.0.0.1 --target-port 9000 --latency 250
```

Every flag is optional and falls back to the same default the GUI
pre-fills when left out:

| Flag | Meaning | Default |
|---|---|---|
| `--listen-addr <address>` | Where SnailNet binds | `127.0.0.1` |
| `--listen-port <port>` | The port your client connects to | `8080` |
| `--target-addr <address>` | The real service | `127.0.0.1` |
| `--target-port <port>` | The real service's port | `80` |
| `--latency <ms>` | Delay on the way back to the client | `250` |

Run `SnailNet.exe --help` (also `-h` or `-?`) to see this from the command
line itself. Once running, it prints the listen and target addresses, the
latency, and every session as it starts and ends:

```
Listening on 127.0.0.1:8080, forwarding to 127.0.0.1:9000, latency 250 ms
Press Ctrl+C to stop.
[14:02:11] Session started (1 active, 1 total)
[14:02:13] Session closed (0 active, 1 total)
```

Press **Ctrl+C** to stop. This drains anything still in flight the same
way the GUI's Stop button does, then exits. A bad flag, a bad value, or an
unrecognised argument is reported on stderr with a non-zero exit code, and
never falls through to the GUI by accident.

An invalid or out-of-range flag never starts the relay. For example:

```console
> SnailNet.exe --listen-port 99999
SnailNet: --listen-port must be a whole number between 1 and 65535
```

## Example: local loopback with ncat

Everything on one machine.

```
    ncat client              SnailNet             ncat server
  127.0.0.1:8080       listen on :8080           127.0.0.1:9000
                        forwards to :9000           --keep-open

         -------------------------------------------->
                       forwarded at once

         <--------------------------------------------
                  held for 2000 ms, then forwarded
```

1. Start a listener to play the "real server":

   ```console
   ncat -l 9000 --keep-open
   ```

2. In SnailNet, set:

   | Field | Value |
   |---|---|
   | Listen address | `127.0.0.1` |
   | Listen port | `8080` |
   | Target address | `127.0.0.1` |
   | Target port | `9000` |
   | Latency (ms) | `2000` |

   Press **Start**.

3. Connect a client to the **listen** address, not the real server:

   ```console
   ncat 127.0.0.1 8080
   ```

4. Type in the client window: it reaches the server instantly. Type in the
   server window: it takes two seconds to reach the client. That is the
   one-way delay.

## Example: remote client, Ubuntu to a Windows target

The listen address is not limited to loopback. SnailNet can bind to a LAN
address so a client on another machine reaches it. Both machines need to be
able to reach each other over the network (same LAN, VPN, and so on).

```
       Ubuntu                   Windows (192.168.1.50)

    ncat client              SnailNet             ncat server
                       listen on 0.0.0.0:8080       --keep-open
                       forwards to 127.0.0.1:9000

      (Windows Defender Firewall must allow inbound TCP 8080)

         -------------------------------------------->
                       forwarded at once

         <--------------------------------------------
                   held for 500 ms, then forwarded
```

**On the Windows machine, running SnailNet:**

1. Find its LAN IP:

   ```console
   ipconfig
   ```

   Look for the IPv4 address under your active adapter, for example
   `192.168.1.50`.

2. Start the same "real server" as above:

   ```console
   ncat -l 9000 --keep-open
   ```

3. In SnailNet, set Listen address to `0.0.0.0` (accepts connections on any
   network interface), Listen port `8080`, Target address `127.0.0.1`,
   Target port `9000`, Latency `500`. Press **Start**.

4. Allow the port through Windows Defender Firewall. SnailNet's listening
   socket is otherwise invisible from off the machine:

   ```powershell
   New-NetFirewallRule -DisplayName "SnailNet 8080" -Direction Inbound -Protocol TCP -LocalPort 8080 -Action Allow
   ```

**On the Ubuntu machine:**

5. Install ncat, if it is not already there:

   ```console
   sudo apt install ncat
   ```

6. Connect to the Windows machine's LAN IP, not loopback:

   ```console
   ncat 192.168.1.50 8080
   ```

7. Type in the Ubuntu client: it reaches the Windows server at once. Type
   in the Windows server: it takes half a second to reach Ubuntu. The delay
   holds even though the client is now a different machine on the network.

## Troubleshooting

| Symptom | Likely cause |
|---|---|
| Start fails immediately, status line names the problem | Read it: it reports a bad port, a blank address, or the specific reason a bind or connect failed, rather than failing silently |
| A remote client cannot connect at all | The firewall rule in the example above is missing, or the two machines cannot otherwise reach each other on the network |
| A client connects but a later one cannot start on the same port | Wait a moment and try again. Windows can hold the previous socket briefly after Stop before the port is free |
| Everything looks right but nothing happens | Confirm the target address and port are the *real* service, and that something is actually listening there |

For anything systematic, the project's own test suite is a good reference
for expected behavior; see [TestGuide.md](TestGuide.md).
