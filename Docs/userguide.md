<p align="center">
  <img src="image.png" alt="SnailNet" width="300">
</p>

# SnailNet User Guide

Version 1.0.0

SnailNet is a small Windows TCP proxy that holds traffic back by a fixed
number of milliseconds. Put it between a client and a server to see how
software behaves over a slow or high-latency link, without changing the
network or either program.

## Table of Contents

- [Getting started](#getting-started)
- [How it works](#how-it-works)
- [Using the window](#using-the-window)
- [Command line mode](#command-line-mode)
- [Example: local loopback with ncat](#example-local-loopback-with-ncat)
- [Example: remote client, Ubuntu to a Windows target](#example-remote-client-ubuntu-to-a-windows-target)
- [More examples](#more-examples)
- [Limits and behavior worth knowing](#limits-and-behavior-worth-knowing)
- [Troubleshooting](#troubleshooting)

## Getting started

**Requirements:** Windows 10 or later, 64-bit.

SnailNet is a single file, `SnailNet.exe`. There is no installer and nothing
else to install alongside it, not even the Visual C++ redistributable. Copy
it anywhere and run it.

- **Double-click it** (or run it with no arguments) to open the window.
- **Run it with any argument** from a terminal to start the relay directly,
  with no window. See [Command line mode](#command-line-mode).

Windows may show a SmartScreen prompt the first time you run a downloaded
executable. Choose **More info**, then **Run anyway**.

## How it works

SnailNet sits between a client and a real server. The client connects to
SnailNet's **listen** address instead of the real one. SnailNet then opens
its own connection to the **target** and forwards bytes between the two.

```text
     client                  SnailNet                  target
                       listen          forward
                       address         to target

        ------------------------------------------------->
                         forwarded at once

        <-------------------------------------------------
                    held for the latency, then forwarded
```

The delay is **one way**: only traffic heading back to the client is held.
A request reaches the target immediately and only the reply is delayed, so a
request and response exchange pays the latency once, not twice.

Each client that connects gets its own connection to the target, so several
clients can be relayed at once without affecting each other's timing.

## Using the window

The window has three tabs: **Relay**, where you set it up and run it;
**Help**, a short version of this guide; and **About**. You can resize or
maximize it, and the fields widen to fit.

On the **Relay** tab, fill in five fields:

| Field | Meaning | Default |
|---|---|---|
| Listen address | Where SnailNet accepts connections. `127.0.0.1` keeps it on this machine; `0.0.0.0` accepts connections from the network | `127.0.0.1` |
| Listen port | The port your client connects to | `8080` |
| Target address | The real server, as an IP address or hostname | `127.0.0.1` |
| Target port | The real server's port | `80` |
| Latency (ms) | Delay applied to traffic on its way back to the client. `0` makes SnailNet a plain relay | `250` |

Press **Start**, then point your client at the listen address and port.
Press **Stop** to close the listener. Stop drops anything still in flight.

The status block below the tabs stays visible from any tab. While the relay
runs, it shows the listen and target addresses, the latency, and how many
sessions are active and how many there have been in total. If something goes
wrong, such as a port that is already in use or a target that refused the
connection, it states the reason there.

## Command line mode

Running `SnailNet.exe` with any argument starts the relay directly in the
terminal, with no window. It uses the same relay and the same input checks as
the window. Only the interface differs.

```console
SnailNet.exe --listen-port 8080 --target-addr 127.0.0.1 --target-port 9000 --latency 250
```

Every flag is optional. A flag you leave out uses the same default the
window starts with:

| Flag | Meaning | Default |
|---|---|---|
| `--listen-addr <address>` | Where SnailNet accepts connections | `127.0.0.1` |
| `--listen-port <port>` | The port your client connects to | `8080` |
| `--target-addr <address>` | The real server | `127.0.0.1` |
| `--target-port <port>` | The real server's port | `80` |
| `--latency <ms>` | Delay on the way back to the client | `250` |
| `--help`, `-h`, `-?` | Show the flags and exit | |

Once running, SnailNet prints its settings, then a timestamped line as each
session starts and ends:

```text
Listening on 127.0.0.1:8080, forwarding to 127.0.0.1:9000, latency 250 ms
Press Ctrl+C to stop.
[14:02:11] Session started (1 active, 1 total)
[14:02:13] Session closed (0 active, 1 total)
```

Press **Ctrl+C** to stop. SnailNet stops the same way the window's Stop
button does, then exits.

### Errors and exit codes

A bad flag or value never starts the relay. SnailNet writes the reason to
stderr and exits with a non-zero code:

```console
> SnailNet.exe --listen-port 99999
SnailNet: --listen-port must be a whole number between 1 and 65535
```

| Exit code | Meaning |
|---|---|
| `0` | Stopped normally with Ctrl+C, or `--help` was shown |
| `1` | A bad argument, a failure to start (for example a port already in use), or the relay stopped unexpectedly |

So a script can check `%ERRORLEVEL%` (cmd) or `$LASTEXITCODE` (PowerShell)
to tell a clean run from a failed one.

## Example: local loopback with ncat

These examples use [ncat](https://nmap.org/ncat/), which comes with Nmap,
as both client and server. That way you can see the delay without a real
application.

Everything here runs on one machine.

```text
    ncat client              SnailNet             ncat server
  127.0.0.1:8080       listen on :8080           127.0.0.1:9000
                        forwards to :9000           --keep-open

         -------------------------------------------->
                       forwarded at once

         <--------------------------------------------
                  held for 2000 ms, then forwarded
```

1. Start a listener to act as the real server:

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

   Press **Start**. Or, from a terminal:

   ```console
   SnailNet.exe --listen-port 8080 --target-port 9000 --latency 2000
   ```

3. Connect a client to the **listen** port, not the real server:

   ```console
   ncat 127.0.0.1 8080
   ```

4. Type in the client window and the text reaches the server instantly. Type
   in the server window and it takes two seconds to reach the client. That is
   the one-way delay.

## Example: remote client, Ubuntu to a Windows target

The listen address does not have to be loopback. SnailNet can listen on a
network address so that a client on another machine can reach it. The two
machines need a network path between them (same LAN, VPN, and so on).

```text
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

1. Find its LAN IP address:

   ```console
   ipconfig
   ```

   Look for the IPv4 address under your active adapter, for example
   `192.168.1.50`.

2. Start the same real server as before:

   ```console
   ncat -l 9000 --keep-open
   ```

3. In SnailNet, set Listen address to `0.0.0.0` (accept connections on any
   network interface), Listen port `8080`, Target address `127.0.0.1`,
   Target port `9000`, Latency `500`. Press **Start**.

4. Allow the port through Windows Defender Firewall from an administrator
   PowerShell. Without this rule, other machines cannot reach the listening
   port:

   ```powershell
   New-NetFirewallRule -DisplayName "SnailNet 8080" -Direction Inbound -Protocol TCP -LocalPort 8080 -Action Allow
   ```

   Remove it when you are done:

   ```powershell
   Remove-NetFirewallRule -DisplayName "SnailNet 8080"
   ```

**On the Ubuntu machine:**

1. Install ncat if you do not already have it:

   ```console
   sudo apt install ncat
   ```

2. Connect to the Windows machine's LAN IP, not loopback:

   ```console
   ncat 192.168.1.50 8080
   ```

3. Type in the Ubuntu client and the text reaches the Windows server at once.
   Type in the Windows server and it takes half a second to reach Ubuntu. The
   delay still applies when the client is on a different machine.

## More examples

Run the file redirection examples from **cmd**, not PowerShell. PowerShell
does not support `<` and re-encodes bytes sent through `>`, which corrupts
binary files.

**Move a file through it.** Set listen `127.0.0.1:8080`, target
`127.0.0.1:9000`, latency `250`, then:

```console
ncat -l 9000 --send-only < big.bin
ncat 127.0.0.1 8080 > out.bin
```

Run the first command as the server and the second as the client. When the
transfer finishes, the two files should be the same size.

**Find where a client gives up.** Point the client at the listen address and
raise the latency between runs until the client stops waiting. That latency
is roughly where the client's timeout is set.

**Watch several connections at once.** Start `ncat -l 9000 --keep-open` as
the server, then open several clients against port `8080`. The active session
count in the status block rises and falls as they connect and disconnect.

## Limits and behavior worth knowing

- **TCP only.** UDP traffic is not relayed.
- **The delay is fixed and one way.** Every chunk of data heading back to the
  client is held for the same number of milliseconds. There is no jitter,
  packet loss, or bandwidth shaping.
- **A long delay also limits throughput.** SnailNet holds at most 1 MB of
  delayed data per connection, so the most it can deliver is roughly
  1 MB divided by the latency in seconds. At 250 ms that is about 4 MB per
  second; at 2000 ms, about 0.5 MB per second.
- **The target is only checked when a client connects.** Pressing Start
  checks that the target address resolves, but not that anything is
  listening there. A target that is down shows up as an error when the first
  client connects, and that client is disconnected.
- **Stop drops in-flight data.** Anything still held in the delay when you
  press Stop or Ctrl+C is discarded.

## Troubleshooting

| Symptom | Likely cause |
|---|---|
| Start fails right away and the status block names the problem | Read the message. It states the bad port, the blank address, or why SnailNet could not listen or connect |
| "Could not start" mentioning the listen address | Another program is already using that port, or the address does not belong to this machine. Choose another port, or use `127.0.0.1` or `0.0.0.0` |
| A remote client cannot connect at all | The firewall rule from the remote example is missing, or the two machines cannot reach each other on the network |
| Start fails on a port you were just using | Windows can keep the previous socket for a short time after Stop. Wait a moment and try again |
| The client connects but is disconnected immediately | Nothing is listening at the target address and port. Check that the real server is running and that you entered its port, not SnailNet's listen port |
| Large transfers are slower than expected | See the throughput limit under [Limits and behavior worth knowing](#limits-and-behavior-worth-knowing). Lower the latency, or expect it |
| The command line says "unrecognised argument" | Check the spelling against `SnailNet.exe --help`. Flags use two dashes |
