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


def test_help_prints_usage_and_exits_clean(exe_path, expected_version):
    """--help names the version, documents every flag, and starts nothing."""
    with CliProcess(exe_path, ["--help"]) as cli:
        code, stdout, stderr = cli.wait()

    assert code == 0, f"--help exited {code}; stderr: {stderr!r}"
    assert stdout.startswith(
        f"SnailNet {expected_version}\n"
    ), f"first line is not the version; stdout: {stdout[:80]!r}"
    assert "--listen-addr" in stdout
    assert "--listen-port" in stdout
    assert "--target-addr" in stdout
    assert "--target-port" in stdout
    assert "--latency" in stdout
    assert "Ctrl+C" in stdout

    assert "--delay-dir" in stdout

    for short in ("-la,", "-lp,", "-ta,", "-tp,", "-d,", "-dd,"):
        assert short in stdout, f"usage does not list {short!r}"


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
    ("listen address too long", ["--listen-addr", "a" * 64], "at most 63"),
    ("target address too long", ["--target-addr", "a" * 64], "at most 63"),
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


#: (short flag, value, text expected in stderr naming the long form)
SHORT_FLAG_REJECTIONS = [
    ("-la", "a" * 64, "--listen-addr must be at most 63"),
    ("-lp", "99999", "--listen-port must be a whole number"),
    ("-ta", "a" * 64, "--target-addr must be at most 63"),
    ("-tp", "0", "--target-port must be a whole number"),
    ("-d", "soon", "latency must be"),
    ("-dd", "sideways", "--delay-dir must be client, target or both"),
]


@pytest.mark.parametrize(
    "flag,value,expected_text",
    SHORT_FLAG_REJECTIONS,
    ids=[case[0] for case in SHORT_FLAG_REJECTIONS],
)
def test_short_flag_matches_long_form(exe_path, flag, value, expected_text):
    """Each short flag selects the same option as its long form.

    A bad value is the cheapest way to tell which option a flag reached:
    every option reports its own error, naming the long form, without
    starting the relay.
    """
    with CliProcess(exe_path, [flag, value]) as cli:
        code, _stdout, stderr = cli.wait()

    assert code != 0, f"{flag}: exited 0 on bad input; stderr was {stderr!r}"
    assert expected_text in stderr, f"{flag}: stderr was {stderr!r}"


@pytest.mark.parametrize("flag", ["-la", "-lp", "-ta", "-tp", "-d", "-dd"])
def test_short_flag_missing_value_is_rejected(exe_path, flag):
    """A short flag given as the last argument, with no value, is refused.

    The message echoes the flag as typed, so the short form appears in it.
    """
    with CliProcess(exe_path, [flag]) as cli:
        code, _stdout, stderr = cli.wait()

    assert code != 0, f"{flag}: exited 0 with no value; stderr was {stderr!r}"
    assert f"{flag} needs a value" in stderr, f"{flag}: stderr was {stderr!r}"


@pytest.mark.parametrize("flag", ["-la", "-ta"])
def test_short_flag_blank_address_is_rejected(exe_path, flag):
    """An explicitly blank address through a short flag is refused too."""
    with CliProcess(exe_path, [flag, ""]) as cli:
        code, _stdout, stderr = cli.wait()

    assert code != 0
    assert "blank" in stderr.lower(), f"{flag}: stderr was {stderr!r}"


@pytest.mark.parametrize(
    "flag,expected_text",
    [
        ("-LP", "--listen-port must be a whole number"),
        ("-Tp", "--target-port must be a whole number"),
        ("-D", "latency must be"),
    ],
)
def test_short_flag_ignores_case(exe_path, flag, expected_text):
    """Short flags match regardless of case, like the long ones."""
    with CliProcess(exe_path, [flag, "abc"]) as cli:
        code, _stdout, stderr = cli.wait()

    assert code != 0
    assert "unrecognised" not in stderr, f"{flag} was not recognised: {stderr!r}"
    assert expected_text in stderr, f"{flag}: stderr was {stderr!r}"


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


def test_longest_address_is_not_rejected_for_length(exe_path, ports):
    """A 63 character address, the most that fits, gets past the length check.

    Pairs with the too-long cases above to pin the limit at exactly 63. The
    name ends in the reserved ``.invalid`` domain, so it fails to resolve at
    once. What matters is that it fails at resolution, not at the length
    check, which shows it was passed on in full rather than cut short.
    """
    longest = "a" * 55 + ".invalid"
    assert len(longest) == 63

    with CliProcess(
        exe_path,
        [
            "--listen-port",
            str(ports.listen),
            "--target-addr",
            longest,
            "--target-port",
            str(ports.target),
        ],
    ) as cli:
        code, _stdout, stderr = cli.wait()

    assert code != 0, "an unresolvable target should not start"
    assert "at most" not in stderr, f"rejected for length; stderr: {stderr!r}"
    assert "could not start" in stderr.lower(), f"stderr: {stderr!r}"


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


def test_relay_runs_with_short_flags_only(exe_path, ports):
    """A relay configured entirely through short flags runs as configured."""
    with (
        EchoServer(ports.target),
        CliProcess(
            exe_path,
            [
                "-la",
                "127.0.0.1",
                "-lp",
                str(ports.listen),
                "-ta",
                "127.0.0.1",
                "-tp",
                str(ports.target),
                "-d",
                "100",
            ],
        ) as cli,
    ):
        with _connect_when_ready(cli, ports.listen) as client:
            client.sendall(b"ping")
            assert client.recv(4) == b"ping"

        _stop(cli)
        _code, stdout, _stderr = cli.wait(timeout=20)

    assert (
        f"Listening on 127.0.0.1:{ports.listen}, forwarding to "
        f"127.0.0.1:{ports.target}, latency 100 ms" in stdout
    ), f"stdout: {stdout!r}"
    assert "Session started (1 active, 1 total)" in stdout


@pytest.mark.parametrize("value", ["sideways", "", "server", "client,target"])
def test_bad_delay_dir_is_rejected(exe_path, value):
    """Anything but client, target or both is refused before starting."""
    with CliProcess(exe_path, ["--delay-dir", value]) as cli:
        code, _stdout, stderr = cli.wait()

    assert code != 0, f"{value!r}: exited 0; stderr was {stderr!r}"
    assert "--delay-dir must be client, target or both" in stderr, stderr


#: (value as typed, inbound held, round trip in multiples of the latency,
#:  how stdout describes it)
CLI_DIRECTION_CASES = [
    ("client", False, 1, "latency 600 ms to client"),
    ("TARGET", True, 1, "latency 600 ms to target"),
    ("both", True, 2, "latency 600 ms each way"),
]


@pytest.mark.parametrize(
    "value,inbound_held,trips,described",
    CLI_DIRECTION_CASES,
    ids=[case[0] for case in CLI_DIRECTION_CASES],
)
def test_delay_dir_holds_the_chosen_direction(
    exe_path, ports, value, inbound_held, trips, described
):
    """--delay-dir picks which direction is held, matched without case.

    The target stamps when the request arrived, which separates a delay on
    the way in from one on the way back.
    """
    delay_ms = 600

    with (
        EchoServer(ports.target) as server,
        CliProcess(
            exe_path,
            [
                "--listen-port",
                str(ports.listen),
                "--target-port",
                str(ports.target),
                "--latency",
                str(delay_ms),
                "--delay-dir",
                value,
            ],
        ) as cli,
    ):
        with _connect_when_ready(cli, ports.listen) as client:
            sent_at = time.monotonic()
            client.sendall(b"ping")
            assert client.recv(4) == b"ping"
            returned_at = time.monotonic()

        assert server.first_byte_at, "the target never saw the request"
        inbound_ms = (server.first_byte_at[0] - sent_at) * 1000.0
        roundtrip_ms = (returned_at - sent_at) * 1000.0

        _stop(cli)
        _code, stdout, _stderr = cli.wait(timeout=20)

    assert described in stdout, f"stdout: {stdout!r}"

    if inbound_held:
        assert inbound_ms >= delay_ms * 0.9, f"inbound took {inbound_ms:.0f} ms"
    else:
        assert inbound_ms < 300, f"inbound took {inbound_ms:.0f} ms"

    expected_ms = delay_ms * trips
    assert roundtrip_ms >= expected_ms * 0.9, f"round trip {roundtrip_ms:.0f} ms"
    assert (
        roundtrip_ms < expected_ms + delay_ms * 0.6 + 400
    ), f"round trip {roundtrip_ms:.0f} ms against {expected_ms} ms expected"


def test_short_delay_dir_flag_works(exe_path, ports):
    """-dd reaches the same option as --delay-dir."""
    with (
        EchoServer(ports.target),
        CliProcess(
            exe_path,
            [
                "-lp",
                str(ports.listen),
                "-tp",
                str(ports.target),
                "-d",
                "50",
                "-dd",
                "both",
            ],
        ) as cli,
    ):
        with _connect_when_ready(cli, ports.listen) as client:
            client.sendall(b"ping")
            assert client.recv(4) == b"ping"

        _stop(cli)
        _code, stdout, _stderr = cli.wait(timeout=20)

    assert "latency 50 ms each way" in stdout, f"stdout: {stdout!r}"


def _localhost_resolves_ipv6_first(port: int) -> bool:
    """Reports whether ``localhost`` resolves to IPv6 before IPv4 here.

    Uses the same resolver SnailNet does, so the answer matches the order
    the relay sees. Windows normally gives ``::1`` then ``127.0.0.1``.

    :param port: Port to resolve alongside the name; any will do.
    :return: True if the first result is IPv6 and an IPv4 result follows.
    """
    try:
        results = socket.getaddrinfo(
            "localhost", port, socket.AF_UNSPEC, socket.SOCK_STREAM
        )
    except OSError:
        return False

    families = [family for family, *_rest in results]
    return (
        len(families) > 1
        and families[0] == socket.AF_INET6
        and socket.AF_INET in families
    )


def test_target_hostname_falls_back_to_next_address(exe_path, ports):
    """A target name that resolves to several addresses tries each in turn.

    ``localhost`` resolves to ``::1`` first, but EchoServer listens only on
    ``127.0.0.1``, so the first address is refused. The relay has to move on
    to the second rather than drop the session. Before it did, every client
    was disconnected with nothing echoed.

    Skipped where ``localhost`` does not resolve to IPv6 first, since the
    first address would then succeed and the test would prove nothing.
    """
    if not _localhost_resolves_ipv6_first(ports.target):
        pytest.skip("localhost does not resolve to ::1 before 127.0.0.1 here")

    data = payload(4 * 1024, seed=31)

    with (
        EchoServer(ports.target),
        CliProcess(
            exe_path,
            [
                "--listen-port",
                str(ports.listen),
                "--target-addr",
                "localhost",
                "--target-port",
                str(ports.target),
                "--latency",
                "0",
            ],
        ) as cli,
    ):
        with _connect_when_ready(cli, ports.listen) as client:
            client.sendall(data)
            client.shutdown(socket.SHUT_WR)

            try:
                total, digest = read_to_eof(client, timeout=20.0)
            except ConnectionResetError:
                total, digest = 0, ""

        _stop(cli)
        _code, stdout, stderr = cli.wait(timeout=20)

    assert total == len(data), (
        f"echoed {total} bytes of {len(data)}; the relay likely tried only ::1. "
        f"stdout: {stdout!r} stderr: {stderr!r}"
    )
    assert digest == digest_of(data), "echoed payload differs from what was sent"
    assert "Session started (1 active, 1 total)" in stdout


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
