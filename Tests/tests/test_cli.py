"""Headless command line mode: argument parsing, the relay, and shutdown.

Mirrors the GUI suite's standard: every test here drives the real
``SnailNet.exe`` as a subprocess and reads its actual stdout, stderr, and
exit code, rather than a stand-in for the parsing or the relay.
"""

import socket
import time

import pytest

from snailnet_tests import CliProcess, EchoServer, digest_of, has_console, payload
from snailnet_tests.support import connect, read_to_eof


def _stop(cli: CliProcess) -> None:
    """Ends a CliProcess the best way this environment allows.

    A real console signal where one is possible, exercising the same
    shutdown path a user pressing Ctrl+C would; a plain kill otherwise.
    Several tests use this purely to end a session that has already
    finished on its own, where a graceful stop is not what is under test, so
    a console-less environment should not cost them a wasted 20 second
    timeout waiting on a signal nothing can deliver; see has_console.
    """
    if has_console():
        cli.send_ctrl_break()
    else:
        cli.stop()


def _connect_when_ready(cli: CliProcess, port: int) -> socket.socket:
    """Connects to the relay once its listener is bound.

    There is no startup signal to wait on, unlike the GUI's Stop button, so
    this retries the connect to cover the gap between the process existing
    and the listener being bound.

    :param cli: The running relay.
    :param port: Its listen port.
    :return: The connected socket.
    """
    for _ in range(50):
        if not cli.alive:
            _code, out, err = cli.wait(timeout=1)
            pytest.fail(f"relay exited before accepting; out={out!r} err={err!r}")

        try:
            return connect(port)
        except OSError:
            time.sleep(0.1)

    pytest.fail("could not connect within the startup window")


def test_help_prints_usage_and_exits_clean(exe_path):
    """--help documents every flag and starts nothing."""
    with CliProcess(exe_path, ["--help"]) as cli:
        code, stdout, stderr = cli.wait()

    assert code == 0, f"--help exited {code}; stderr: {stderr!r}"
    assert "--listen-addr" in stdout
    assert "--listen-port" in stdout
    assert "--target-addr" in stdout
    assert "--target-port" in stdout
    assert "--latency" in stdout
    assert "Ctrl+C" in stdout


@pytest.mark.parametrize("flag", ["-h", "-?"])
def test_help_aliases_match_long_form(exe_path, flag):
    """-h and -? are accepted as aliases for --help."""
    with CliProcess(exe_path, [flag]) as cli:
        code, stdout, _stderr = cli.wait()

    assert code == 0
    assert "--listen-addr" in stdout


#: (label, argv, text expected somewhere in stderr)
REJECTED_ARGS = [
    ("unknown flag", ["--not-a-real-flag"], "unrecognised"),
    ("missing value", ["--listen-port"], "needs a value"),
    ("port below range", ["--listen-port", "0"], "between"),
    ("port above range", ["--target-port", "65536"], "between"),
    ("non numeric port", ["--listen-port", "abc"], "between"),
    ("non numeric latency", ["--latency", "soon"], "latency"),
    ("negative latency", ["--latency", "-5"], "latency"),
]


@pytest.mark.parametrize(
    "label,argv,expected_text",
    REJECTED_ARGS,
    ids=[case[0].replace(" ", "_") for case in REJECTED_ARGS],
)
def test_bad_argument_is_rejected(exe_path, label, argv, expected_text):
    """Each bad argument is reported on stderr with a non-zero exit.

    None of these may start the relay: a process that bound a port on bad
    input would be much harder to notice than one that simply refused.

    :param exe_path: Executable under test.
    :param label: Human readable case name.
    :param argv: Arguments to pass.
    :param expected_text: Substring expected somewhere in stderr.
    """
    with CliProcess(exe_path, argv) as cli:
        code, _stdout, stderr = cli.wait()

    assert code != 0, f"{label}: exited 0 on bad input; stderr was {stderr!r}"
    assert expected_text in stderr.lower(), f"{label}: stderr was {stderr!r}"


def test_blank_address_is_rejected(exe_path):
    """An explicitly blank address is refused, not treated as a default.

    Needs its own test because shells differ on whether an empty string
    survives as an argument at all; CliProcess passes argv directly, so
    this is unambiguous.
    """
    with CliProcess(exe_path, ["--listen-addr", ""]) as cli:
        code, _stdout, stderr = cli.wait()

    assert code != 0
    assert "blank" in stderr.lower()


def test_relay_works_and_reports_sessions(exe_path, ports):
    """A real session through CLI mode is byte exact, delayed one way, and
    reported on stdout with session counts."""
    data = payload(64 * 1024, seed=23)

    with (
        EchoServer(ports.target),
        CliProcess(
            exe_path,
            [
                "--listen-port",
                str(ports.listen),
                "--target-port",
                str(ports.target),
                "--latency",
                "300",
            ],
        ) as cli,
    ):
        with _connect_when_ready(cli, ports.listen) as client:
            client.sendall(data)
            client.shutdown(socket.SHUT_WR)
            total, digest = read_to_eof(client)

        _stop(cli)
        _code, stdout, _stderr = cli.wait(timeout=20)

    assert total == len(data), f"echoed {total} bytes, sent {len(data)}"
    assert digest == digest_of(data), "echoed payload differs from what was sent"
    assert "Listening on 127.0.0.1" in stdout
    assert "latency 300 ms" in stdout
    assert "Session started (1 active, 1 total)" in stdout
    assert "Session closed (0 active, 1 total)" in stdout


def test_omitted_flags_use_the_gui_defaults(exe_path, ports):
    """Flags left out fall back to the same defaults the GUI pre-fills.

    Only the two port flags are given, pointed at this test's own ports;
    listen and target address are left to default to 127.0.0.1, and
    latency to default to 250 ms, which the timing below confirms rather
    than assumes.
    """
    with (
        EchoServer(ports.target),
        CliProcess(
            exe_path,
            ["--listen-port", str(ports.listen), "--target-port", str(ports.target)],
        ) as cli,
    ):
        with _connect_when_ready(cli, ports.listen) as client:
            sent_at = time.monotonic()
            client.sendall(b"ping")
            assert client.recv(4) == b"ping"
            elapsed_ms = (time.monotonic() - sent_at) * 1000.0

        _stop(cli)
        _code, stdout, _stderr = cli.wait(timeout=20)

    assert (
        "latency 250 ms" in stdout
    ), f"default latency not applied; stdout: {stdout!r}"
    assert (
        elapsed_ms >= 200
    ), f"round trip was {elapsed_ms:.0f} ms; default latency may be unused"


@pytest.mark.skipif(
    not has_console(),
    reason="GenerateConsoleCtrlEvent needs this process to have a real console",
)
def test_console_signal_stops_cleanly(exe_path, ports):
    """A console signal stops the relay the same way the GUI's Stop does.

    Sends Ctrl+Break, which SnailNet handles in the same case as Ctrl+C; see
    CTRL_BREAK_EVENT in snailnet_tests.cli for why Ctrl+C itself cannot be
    sent from here. Skipped when this process has no console of its own;
    see developerguide.md.
    """
    with (
        EchoServer(ports.target),
        CliProcess(
            exe_path,
            ["--listen-port", str(ports.listen), "--target-port", str(ports.target)],
        ) as cli,
    ):
        _connect_when_ready(cli, ports.listen).close()

        cli.send_ctrl_break()
        code, stdout, _stderr = cli.wait(timeout=20)

    assert code == 0, f"signalled shutdown exited {code}; stdout: {stdout!r}"
    assert "Stopping" in stdout
    assert "Stopped" in stdout

    # The listener has to be genuinely gone, not just the process winding
    # down with the socket still held.
    with pytest.raises(OSError):
        with connect(ports.listen, timeout=3.0):
            pass
