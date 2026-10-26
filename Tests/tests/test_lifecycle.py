"""Start, stop, restart, and failure reporting.

Where the correctness tests ask whether the right bytes moved, these ask
whether the application can be driven repeatedly without wedging, leaking a
port, or going quiet about a failure.
"""

import socket

import pytest
from conftest import assert_started

from snailnet_tests import EchoServer, SourceServer, payload
from snailnet_tests.support import connect


def test_stop_returns_to_idle_and_frees_the_port(app, ports):
    """Stop closes the listener and the application comes back to idle."""
    with EchoServer(ports.target):
        app.configure("127.0.0.1", ports.listen, "127.0.0.1", ports.target, 0)
        app.start()
        assert_started(app)

        with connect(ports.listen) as client:
            client.sendall(b"hello")
            assert client.recv(5) == b"hello", "the relay was not actually working"

        app.stop()
        assert app.wait_idle(), f"never returned to idle; status {app.status_line()!r}"
        assert app.alive, "the application died during Stop"

        # Idle has to mean the listener is really gone, not just that the
        # button flipped.
        with pytest.raises(OSError):
            with connect(ports.listen, timeout=3.0):
                pass


def test_restart_on_the_same_port(app, ports):
    """Start, Stop, and Start again on the same port, repeatedly.

    Windows refuses a rebind while the previous listening socket sits in
    ``TIME_WAIT`` unless the socket opts out, so a relay that can be started
    once but not twice fails here and nowhere else.
    """
    with EchoServer(ports.target):
        for attempt in (1, 2, 3):
            app.configure("127.0.0.1", ports.listen, "127.0.0.1", ports.target, 0)
            app.start()

            if not app.wait_running():
                pytest.fail(
                    f"start #{attempt} on port {ports.listen} failed; "
                    f"status {app.status_line()!r}"
                )

            with connect(ports.listen) as client:
                client.sendall(b"x")
                assert client.recv(1) == b"x", f"relay dead on start #{attempt}"

            app.stop()
            assert app.wait_idle(), f"stop #{attempt} never finished"

    assert app.alive, "the application died over the restart cycle"


@pytest.mark.slow
def test_stop_mid_transfer_does_not_hang(app, ports):
    """Stop with bytes in flight neither hangs nor crashes the application.

    ``RelayStop`` blocks the UI thread until every worker exits, so a worker
    that misses the abort signal shows up here as a hung window rather than
    as an error.
    """
    data = payload(32 * 1024 * 1024, seed=17)

    with SourceServer(ports.target, data):
        app.configure("127.0.0.1", ports.listen, "127.0.0.1", ports.target, 1000)
        app.start()
        assert_started(app)

        client = connect(ports.listen, timeout=60.0)

        try:
            assert client.recv(4096), "the transfer never started"
            app.stop()
            assert app.wait_idle(
                timeout=40
            ), f"Stop hung with a transfer in flight; status {app.status_line()!r}"
        finally:
            client.close()

    assert app.alive, "the application died when stopped mid transfer"


def test_dead_target_is_reported(app, ports):
    """A target that resolves but refuses connections surfaces an error.

    The target is only resolve-checked at Start, so this failure cannot
    appear until a client actually arrives. It has to be reported, and the
    client has to be dropped rather than left waiting on a reply that will
    never come.
    """
    # Nothing is listening on ports.target.
    app.configure("127.0.0.1", ports.listen, "127.0.0.1", ports.target, 0)
    app.start()
    assert_started(app)

    with connect(ports.listen) as client:
        client.settimeout(25.0)

        try:
            leftover = client.recv(64)
            assert leftover == b"", f"expected EOF from a dead target, got {leftover!r}"
        except socket.timeout:
            pytest.fail("the relay left the client hanging on a dead target")
        except ConnectionResetError:
            pass  # An abortive close is an acceptable way to say no.

    # Matched on the error itself: the session counters also change the
    # status when the client arrives, so "it changed" proves nothing.
    status = app.wait_status(lambda text: "Error:" in text, timeout=15)
    assert "Error:" in status, f"the failure was never reported; status {status!r}"
    assert app.alive, "the application died on a refused target"


def test_bind_conflict_is_reported(app, ports):
    """A listen port another process holds is reported, not half started."""
    holder = socket.socket()
    holder.bind(("127.0.0.1", 0))
    holder.listen(1)
    taken = holder.getsockname()[1]

    try:
        app.configure("127.0.0.1", taken, "127.0.0.1", ports.target, 0)
        app.start()
        status = app.wait_status(lambda text: "Could not start" in text)

        assert not app.running, (
            f"the relay claims to be listening on a port held elsewhere; "
            f"status {status!r}"
        )
        assert app.alive, "the application died on a bind conflict"
        assert "Could not start" in status, f"conflict not reported; status {status!r}"
    finally:
        holder.close()
