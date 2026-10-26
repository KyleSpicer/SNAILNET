<p align="center">
  <img src="image.png" alt="SnailNet" width="300">
</p>

# SnailNet Test Guide

How to run the test suite and understand what it tells you.

No prior knowledge of the test suite is assumed. If you only want to run the
tests and read the output, the first two sections are enough.

## Table of Contents

- [Building and running](#building-and-running)
- [Reading the results](#reading-the-results)
- [How the suite works](#how-the-suite-works)
- [What each test proves](#what-each-test-proves)
- [Running part of the suite](#running-part-of-the-suite)
- [When something fails](#when-something-fails)
- [API documentation](#api-documentation)
- [Keeping the suite healthy](#keeping-the-suite-healthy)

## Building and running

Clone and build SnailNet as in the README's
[Quick start](../README.md#quick-start), which also lists the tools you need.
Visual Studio is needed even if you only care about the tests: the suite
drives the real binary, so one has to exist. Then, from `Tests`:

```console
poetry install
poetry run pytest
```

The suite expects the executable at
`SnailNet/Build/x64/Release/SnailNet.exe`. To test a different build, point it
there:

```console
poetry run pytest --exe ..\SnailNet\Build\x64\Debug\SnailNet.exe
```

**Windows only.** The suite drives a Win32 window through `user32.dll`, so it
does not run on Linux or macOS. It has no runtime dependencies of its own; it
talks to Windows directly through `ctypes`.

**Windows will show SnailNet's window opening and closing repeatedly while the
tests run.** That is the suite working, not a fault. It launches a fresh copy
of the application for each test and closes it afterwards. Do not click on the
windows while the suite is running.

## Reading the results

A full run takes about 45 seconds and ends like this:

```text
tests/test_cli.py::test_help_prints_usage_and_exits_clean PASSED          [  2%]
tests/test_cli.py::test_help_aliases_match_long_form[-h] PASSED           [  4%]
...
tests/test_validation.py::test_zero_latency_is_accepted PASSED            [100%]

========================= 42 passed in 43.80s =========================
```

`42 passed` is what you want. Run from a terminal without a console of its
own (some task runners and CI setups), it is `41 passed, 1 skipped` instead,
which is also fine; see
[Command line mode: `test_cli.py`](#command-line-mode-test_clipy). Anything
else needs reading.

### The words in the output

| Word | Meaning |
|---|---|
| `PASSED` | The test's checks all held |
| `FAILED` | A check did not hold. Something is wrong with SnailNet, or with the test |
| `ERROR` | The test could not run at all, usually a setup problem such as a missing executable |
| `SKIPPED` | The test was deliberately not run. `-m "not slow"` does this on purpose; `test_console_signal_stops_cleanly` does it on its own when there is no console to test through |
| `42 passed` | Count of tests, not of checks. Most tests make several assertions |

The `[ 42%]` on the right is progress through the run, nothing more.

### A failure, read line by line

A failure prints the test, the code around it, and the reason:

```text
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

```text
E  AssertionError: received 0 bytes of 8388608; relay reported
E  'Listening on: 127.0.0.1:8151 | Forwarding to: 127.0.0.1:8152 |
E   Latency: 100 ms to client | Error: The target refused the connection'
```

That is SnailNet's own status text, captured at the moment the test gave up.

If a CI system or test viewer needs a machine-readable record, add
`--junitxml=reports/junit.xml` to the command; nothing is written by default.

## How the suite works

SnailNet has two front ends, a window and a command line, over the same
relay. It has no API to call, so the suite **drives the real application
the way a person or a script would**, through both.

For the window (`app.py`), each test:

1. Launches `SnailNet.exe`.
2. Finds its window and waits for the controls to exist.
3. Types into the five fields.
4. Presses **Start**.
5. Opens real TCP connections through the relay and checks what comes out
   the other side.
6. Presses **Stop** and closes the application.

For the command line (`cli.py`), each test launches `SnailNet.exe` with
arguments, reads its stdout and stderr, and checks the exit code, moving
real traffic through it the same way where the test calls for it.

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
| `Tests/tests/test_cli.py` | Argument parsing, the relay via the command line, and shutdown on a console signal |

### Two details that are easy to break

Fields are set by sending `WM_SETTEXT` to each control, and each window is
matched to the process the suite launched. The docstrings in
`Tests/snailnet_tests/app.py` explain why; read them before changing how the
suite finds or fills in the window.

## What each test proves

### Correctness: `test_correctness.py`

| Test | What it proves |
|---|---|
| `roundtrip_is_byte_exact` | Data through the relay comes back unchanged |
| `latency_applies_one_way` | The delay applies to target-to-client traffic only. Measured in **both** directions, so a delay applied twice fails rather than passing as "slow enough" |
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
| `console_signal_stops_cleanly` | A console signal stops the relay the same way the GUI's Stop button does, exits 0, and frees the port |

`console_signal_stops_cleanly` sends Ctrl+Break, not Ctrl+C. Windows
disables Ctrl+C in a process started in its own process group, which the
suite needs in order to signal SnailNet without also signalling itself.
SnailNet handles both keys in the same place, so the shutdown path tested is
the one a user's Ctrl+C takes.

Sending either needs the process running the suite to have a real console
(`GenerateConsoleCtrlEvent` is silently a no-op without one). A background
task runner or some CI setups lack one, so the test checks for this and
skips rather than hangs, and the other tests here end SnailNet with a kill
instead. See [developerguide.md](developerguide.md#command-line-mode).

## Running part of the suite

```console
poetry run pytest -m "not slow"              # skip the megabyte transfers, ~20s
poetry run pytest tests/test_lifecycle.py    # one file
poetry run pytest -k latency                 # tests whose name contains "latency"
poetry run pytest -x                          # stop at the first failure
poetry run pytest -q                          # one line per test
poetry run pytest --durations=10              # show the ten slowest
poetry run pytest -o log_cli=true             # print each command a test runs
poetry run pytest --report=reports/report.html  # write an HTML report
```

Tests marked `slow` move several megabytes or wait out a long delay. They are
the ones most likely to catch a data-loss bug, so prefer the full suite before
merging anything.

The `--report` file is plain HTML with no scripts, so it opens in any browser
or file viewer. Each row shows the test's result, what it checks (its
docstring), and an expandable **Commands and output** section listing the
exact `SnailNet.exe` commands it ran, their exit codes, and any failure
output. CI writes one for the command line tests and publishes it as the
`cli-test-report` artifact.

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

This guide covers running the suite and reading its results. The API of
the harness itself (`SnailNet`, `CliProcess`, the test servers, and the
support helpers) is documented separately, generated by Sphinx autodoc from
the docstrings in `Tests/snailnet_tests`. You need it when writing a new
test, not when running the existing ones:

```console
cd Tests
poetry install --with docs
poetry run sphinx-build -b html docs docs/_build/html
```

Open `Tests/docs/_build/html/index.html`.

The generated pages are not copied into this guide. The docstrings are the
source of truth, so a copy here would go stale the first time a signature
changed. If you change a public function in `snailnet_tests`, update its
docstring and the rendered docs follow.

## Keeping the suite healthy

The repository's `pre-commit` hooks (black, isort, flake8, pylint) cover the
Python here and run on every commit. To check the suite without committing,
from the repository root:

```console
pre-commit run --files Tests/snailnet_tests/*.py Tests/tests/*.py Tests/docs/conf.py
```

One thing the tools cannot check: the control identifiers in
`Tests/snailnet_tests/app.py` are copied from `SNAILNET_CTL_ID` in
`SnailNet/Snailnet.h`. **Renumbering that enum breaks every test here**, and
the failure looks like the application ignoring its fields rather than like a
mismatch. If you change those identifiers, change both.
