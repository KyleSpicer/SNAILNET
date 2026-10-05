<p align="center">
  <img src="Images/snailnet-color.png" alt="SnailNet" width="300">
</p>

# SnailNet Test Guide

How to get the code, run the test suite, and understand what it tells you.

No prior knowledge of the test suite is assumed. If you only want to run the
tests and read the output, the first three sections are enough.

## Table of Contents

- [Getting the code](#getting-the-code)
- [What you need installed](#what-you-need-installed)
- [Build and test in five commands](#build-and-test-in-five-commands)
- [Reading the results](#reading-the-results)
- [The JUnit XML report](#the-junit-xml-report)
- [How the suite works](#how-the-suite-works)
- [What each test proves](#what-each-test-proves)
- [Running part of the suite](#running-part-of-the-suite)
- [When something fails](#when-something-fails)
- [API documentation](#api-documentation)

## Getting the code

```console
git clone git@gitea.com:4-Projects/SNAILNET.git
cd SNAILNET
```

If you have not set up an SSH key with Gitea, clone over HTTPS instead:

```console
git clone https://gitea.com/4-Projects/SNAILNET.git
cd SNAILNET
```

To work on a specific branch rather than `main`:

```console
git fetch origin
git switch 6-testing
```

## What you need installed

| Tool | Why | Check it |
|---|---|---|
| Visual Studio 2026, **Desktop development with C++** | Builds `SnailNet.exe`. The tests drive the real binary, so one has to exist | Open `SnailNet/SnailNet.slnx` |
| Python 3.11 or newer | Runs the test suite | `python --version` |
| [Poetry](https://python-poetry.org/docs/#installation) | Installs the suite's dependencies into its own virtual environment | `poetry --version` |

No other dependencies. The suite talks to Windows directly through `ctypes`,
and SnailNet itself links only against the Windows SDK.

**Windows only.** The suite drives a Win32 window through `user32.dll`, so it
does not run on Linux or macOS.

## Build and test in five commands

From the repository root:

```console
cd SnailNet
msbuild SnailNet.slnx /p:Configuration=Release /p:Platform=x64
cd ..\Tests
poetry install
poetry run pytest
```

If `msbuild` is not on your `PATH`, either build from inside Visual Studio
(Ctrl+Shift+B with the configuration set to **x64 Release**), or use the full
path:

```console
& "C:\Program Files\Microsoft Visual Studio\18\Community\MSBuild\Current\Bin\MSBuild.exe" `
    SnailNet.slnx /p:Configuration=Release /p:Platform=x64
```

The suite expects the executable at
`SnailNet/Build/x64/Release/SnailNet.exe`. To test a different build, point it
there:

```console
poetry run pytest --exe ..\SnailNet\Build\x64\Debug\SnailNet.exe
```

**Windows will show SnailNet's window opening and closing repeatedly while the
tests run.** That is the suite working, not a fault. It launches a fresh copy
of the application for each test and closes it afterwards. Do not click on the
windows while the suite is running.

## Reading the results

A full run takes about 40 seconds and ends like this:

```
tests/test_correctness.py::test_roundtrip_is_byte_exact PASSED            [  2%]
tests/test_correctness.py::test_latency_applies_one_way PASSED            [  5%]
tests/test_correctness.py::test_large_delayed_download_is_whole PASSED    [  7%]
...
tests/test_cli.py::test_ctrl_c_stops_cleanly SKIPPED (...no console...) [100%]

================= 41 passed, 1 skipped in 42.03s =================
```

`41 passed` is what you want. A `skipped` alongside it is expected if the
environment running the suite has no console of its own; see
[Command line mode: `test_cli.py`](#command-line-mode-test_clipy). Anything
else needs reading.

### The words in the output

| Word | Meaning |
|---|---|
| `PASSED` | The test's checks all held |
| `FAILED` | A check did not hold. Something is wrong with SnailNet, or with the test |
| `ERROR` | The test could not run at all, usually a setup problem such as a missing executable |
| `SKIPPED` | The test was deliberately not run. `-m "not slow"` does this on purpose; `test_ctrl_c_stops_cleanly` does it on its own when there is no console to test through |
| `41 passed` | Count of tests, not of checks. Most tests make several assertions |

The `[ 42%]` on the right is progress through the run, nothing more.

### A failure, read line by line

A failure prints the test, the code around it, and the reason:

```
____________________ test_latency_applies_one_way ____________________

>       assert inbound_ms < 400, (
            f"client->target took {inbound_ms:.0f} ms; that direction is "
            "supposed to be undelayed"
        )
E       AssertionError: client->target took 1500 ms; that direction is
E       supposed to be undelayed
E       assert 1499.524199985899 < 400

tests\test_correctness.py:77: AssertionError
```

Read it bottom up:

1. **`tests\test_correctness.py:77`**: where the check lives.
2. **`assert 1499.5 < 400`**: the comparison that failed, with real numbers.
3. **The message**: what that means in plain terms. Here, traffic heading
   *to* the target was delayed by 1.5 seconds when it should not have been
   delayed at all. SnailNet's delay is supposed to apply in one direction
   only, so this says the delay is being applied in both.

Every assertion in this suite carries a message like that. The numbers in it
are the actual measurements, so a failure usually tells you what broke
without needing to open the test.

Where the relay reports a reason of its own, the failure includes it:

```
E  AssertionError: received 0 bytes of 8388608; relay reported
E  'Listening on: 127.0.0.1:8151 | Forwarding to: 127.0.0.1:8152 |
E   Latency: 100 ms to client | Error: The target refused the connection'
```

That is SnailNet's own status text, captured at the moment the test gave up.

## The JUnit XML report

Every run also writes `Tests/reports/junit.xml`, picked up automatically:
nothing extra to pass on the command line. It is a build artifact rather than
something to read by eye: one `<testcase>` element per test, with a
`<failure>` child and the same message you saw in the console for anything
that failed. This is the format most CI systems and test result viewers
understand natively, which is the point of it; for a person, the console
output above is still the more readable record of a run.

The file is overwritten on every run and is gitignored, so it never needs
committing or cleaning up by hand.

## How the suite works

SnailNet is a window with five fields and two buttons. It has no command
line, so there is no way to run it headlessly and no API to call.

The suite therefore **drives the real application the way a person would**.
For each test it:

1. Launches `SnailNet.exe`.
2. Finds its window and waits for the controls to exist.
3. Types into the five fields.
4. Presses **Start**.
5. Opens real TCP connections through the relay and checks what comes out
   the other side.
6. Presses **Stop** and closes the application.

Nothing is simulated. There is no mock relay and no relinking of SnailNet's
object files into a test program. The suite stands up throwaway TCP servers
on loopback, points SnailNet at them, and measures what actually arrives.
**A test that passes here passed against the binary that ships.**

### The parts

| Path | What it does |
|---|---|
| `Tests/snailnet_tests/app.py` | Launches and drives one instance through its window |
| `Tests/snailnet_tests/cli.py` | Launches one instance in headless mode and reads its stdout/stderr/exit code |
| `Tests/snailnet_tests/servers.py` | Echo, source, and sink servers for SnailNet to relay to |
| `Tests/snailnet_tests/support.py` | Ports, test payloads, stream readers |
| `Tests/tests/test_correctness.py` | Does the right data arrive, at the right time |
| `Tests/tests/test_lifecycle.py` | Start, stop, restart, and error reporting |
| `Tests/tests/test_validation.py` | Bad input is refused |
| `Tests/tests/test_cli.py` | Argument parsing, the relay via the command line, and Ctrl+C shutdown |

### Two details worth knowing

**Field values are set with window messages, not `SetDlgItemText`.** Across a
process boundary, `SetWindowText` updates a cached copy of the window text
that the edit control itself ignores. A test using it reads back exactly what
it wrote, is satisfied, and drives an application still holding its defaults.
The suite sends `WM_SETTEXT` straight to each control instead.

**Each instance is matched to its own process.** `FindWindow` returns any
window of the class, and an instance asked to close stays alive for as long as
its shutdown takes. Matching the window to the process the suite launched is
what keeps one test from driving the previous test's dying copy.

Both of these were bugs in the suite during development. They are recorded
here because they are easy to reintroduce.

## What each test proves

### Correctness: `test_correctness.py`

| Test | What it proves |
|---|---|
| `roundtrip_is_byte_exact` | Data through the relay comes back unchanged |
| `latency_applies_one_way` | The delay applies to target→client only. Measured in **both** directions, so a delay applied twice fails rather than passing as "slow enough" |
| `large_delayed_download_is_whole` | 8 MB through the delayed direction arrives whole, past the 1 MB queue cap, forcing back pressure repeatedly |
| `large_undelayed_upload_is_whole` | 8 MB through the undelayed direction arrives whole |
| `half_close_lets_the_reply_through` | A client that finishes sending still receives the full response. A relay treating that as a full shutdown truncates the reply |
| `concurrent_sessions_do_not_cross` | 20 clients at once each get their own stream. Each sends different data, so mixed-up buffers fail the checksum |
| `slow_client_still_gets_every_byte` | A client reading slower than the server sends still gets everything, with the relay held against a full send buffer throughout |

### Lifecycle: `test_lifecycle.py`

| Test | What it proves |
|---|---|
| `stop_returns_to_idle_and_frees_the_port` | Stop really releases the port, not just greys out the button |
| `restart_on_the_same_port` | Start/Stop three times on one port. Windows blocks a rebind while the old socket lingers, so this is where that would show |
| `stop_mid_transfer_does_not_hang` | Stop during a 32 MB transfer neither hangs nor crashes |
| `dead_target_is_reported` | A target that is not listening is reported, and the client is dropped rather than left waiting |
| `bind_conflict_is_reported` | A port another program holds is reported, not half started |

### Validation: `test_validation.py`

Fourteen bad inputs, each of which must leave the relay idle: ports of 0,
65536 and 999999, non-numeric and empty ports, blank addresses, non-numeric
and negative latency, an unresolvable hostname, and an address that cannot be
bound.

Two tests then confirm the checks are not simply refusing everything:
port 65535 is accepted (so the range check is not off by one), and a latency
of zero is accepted (it turns SnailNet into a plain relay).

### Command line mode: `test_cli.py`

| Test | What it proves |
|---|---|
| `help_prints_usage_and_exits_clean` | `--help` documents every flag and starts nothing |
| `help_aliases_match_long_form` | `-h` and `-?` behave the same as `--help` |
| `bad_argument_is_rejected` | An unknown flag, a missing value, an out of range or non-numeric port, and bad latency are all reported on stderr with a non-zero exit, never a half start |
| `blank_address_is_rejected` | An address that is explicitly empty is refused, not quietly treated as a default |
| `relay_works_and_reports_sessions` | A real session through the CLI is byte exact, delayed one way, and the session start/end is printed with live counts |
| `omitted_flags_use_the_gui_defaults` | Leaving a flag out falls back to the same default the GUI pre-fills, confirmed by timing, not just by reading the help text |
| `ctrl_c_stops_cleanly` | Ctrl+C drains and stops the relay the same way the GUI's Stop button does |

`ctrl_c_stops_cleanly` needs a real console to send a real Ctrl+C through
(`GenerateConsoleCtrlEvent` is silently a no-op without one), which the
process running the suite does not always have: a background task runner
or certain CI setups can lack one entirely. The test checks for this itself
and skips rather than hangs; the shutdown path was confirmed by hand against
a real console during development. See
[DeveloperGuide.md](DeveloperGuide.md#command-line-mode) for the detail.

## Running part of the suite

```console
poetry run pytest -m "not slow"              # skip the megabyte transfers, ~20s
poetry run pytest tests/test_lifecycle.py    # one file
poetry run pytest -k latency                 # tests whose name contains "latency"
poetry run pytest -x                          # stop at the first failure
poetry run pytest -q                          # one line per test
poetry run pytest --durations=10              # show the ten slowest
```

Tests marked `slow` move several megabytes or wait out a long delay. They are
the ones most likely to catch a data-loss bug, so prefer the full suite before
merging anything.

## When something fails

| Symptom | Likely cause |
|---|---|
| `UsageError: SnailNet.exe not found at ...` | The application has not been built, or was built in a different configuration. Build x64 Release, or pass `--exe` |
| `SnailNetError: SnailNet window did not appear` | The executable failed to start. Try launching it by hand |
| Every test fails on `connect` | Something else is holding the ports, or a firewall is blocking loopback |
| One `slow` test fails alone | Re-run it on its own. If it passes, suspect machine load before suspecting the relay |
| A stray `SnailNet.exe` is left running | The suite kills leftovers when it starts and finishes. To clear one by hand: `taskkill /F /IM SnailNet.exe` |

A failure in a `slow` test that reports a **byte count short of the expected
total** is the one to take most seriously. That means the relay lost data,
which is the worst thing a proxy can do, and is worth reporting as an issue
with the full output attached.

## API documentation

The suite's modules are documented from their own docstrings with Sphinx:

```console
cd Tests
poetry install --with docs
poetry run sphinx-build -b html docs docs/_build/html
```

Open `Tests/docs/_build/html/index.html`.

## Keeping the suite healthy

The repository's `pre-commit` hooks cover the Python here. Before committing
changes to the suite:

```console
cd Tests
poetry run black --line-length=88 snailnet_tests tests docs
poetry run isort --profile=black snailnet_tests tests docs
poetry run flake8 --max-line-length=88 snailnet_tests tests docs
poetry run pylint --disable=R --enable=C0114,C0115,C0116 snailnet_tests tests docs
```

One thing the tools cannot check: the control identifiers in
`Tests/snailnet_tests/app.py` are copied from `SNAILNET_CTL_ID` in
`SnailNet/Snailnet.h`. **Renumbering that enum breaks every test here**, and
the failure looks like the application ignoring its fields rather than like a
mismatch. If you change those identifiers, change both.
