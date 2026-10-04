"""Stream integrity and latency: does the relay move the right bytes, late.

These are the tests that would catch the failures a delaying proxy is prone
to — truncating a transfer at a queue boundary, collapsing a half close into
a full one, applying the delay in both directions, or interleaving two
clients' streams.
"""

import hashlib
import socket
import threading
import time

import pytest
from conftest import assert_started

from snailnet_tests import (
    EchoServer,
    SinkServer,
    SourceServer,
    digest_of,
    payload,
    read_to_eof,
)
from snailnet_tests.support import connect

#: Exceeds RELAY_QUEUE_MAX_CB (1 MB) several times over, so the reader has to
#: stop and restart on back pressure repeatedly without losing a chunk.
LARGE_CB = 8 * 1024 * 1024


def test_roundtrip_is_byte_exact(app, ports):
    """Bytes survive a round trip through the relay unchanged."""
    data = payload(64 * 1024, seed=7)

    with EchoServer(ports.target):
        app.configure("127.0.0.1", ports.listen, "127.0.0.1", ports.target, 0)
        app.start()
        assert_started(app)

        with connect(ports.listen) as client:
            client.sendall(data)
            client.shutdown(socket.SHUT_WR)
            total, digest = read_to_eof(client)

    assert total == len(data), f"echoed {total} bytes, sent {len(data)}"
    assert digest == digest_of(data), "echoed payload differs from what was sent"


def test_latency_applies_one_way(app, ports):
    """The delay applies to target->client only.

    This is the project's defining behaviour, so it is measured in both
    directions rather than inferred from a single round trip. An
    implementation that delayed both ways would still look correct to a test
    that only checked the total.
    """
    delay_ms = 1500

    with EchoServer(ports.target) as server:
        app.configure("127.0.0.1", ports.listen, "127.0.0.1", ports.target, delay_ms)
        app.start()
        assert_started(app)

        with connect(ports.listen) as client:
            sent_at = time.monotonic()
            client.sendall(b"ping")
            assert client.recv(4) == b"ping"
            returned_at = time.monotonic()

        assert server.first_byte_at, "the target never saw the request"
        inbound_ms = (server.first_byte_at[0] - sent_at) * 1000.0
        roundtrip_ms = (returned_at - sent_at) * 1000.0

    assert inbound_ms < 400, (
        f"client->target took {inbound_ms:.0f} ms; that direction is "
        "supposed to be undelayed"
    )
    assert roundtrip_ms >= delay_ms * 0.9, (
        f"round trip was {roundtrip_ms:.0f} ms; expected at least the "
        f"configured {delay_ms} ms"
    )
    # Catches the delay being applied to both directions, which would roughly
    # double this without failing any single-direction check.
    assert roundtrip_ms < delay_ms * 1.6 + 500, (
        f"round trip was {roundtrip_ms:.0f} ms against {delay_ms} ms "
        "configured; the delay may be applied in both directions"
    )


@pytest.mark.slow
def test_large_delayed_download_is_whole(app, ports):
    """A multi-megabyte delayed transfer arrives whole and byte exact.

    The delayed direction is the one with a queue in it, so this is where a
    back pressure bug would drop or duplicate a chunk.
    """
    data = payload(LARGE_CB, seed=11)

    with SourceServer(ports.target, data):
        app.configure("127.0.0.1", ports.listen, "127.0.0.1", ports.target, 100)
        app.start()
        assert_started(app)

        with connect(ports.listen, timeout=180.0) as client:
            total, digest = read_to_eof(client, timeout=180.0)

        reported = app.status_line()

    assert total == len(
        data
    ), f"received {total} bytes of {len(data)}; relay reported {reported!r}"
    assert digest == digest_of(data), "received payload differs from the source"


@pytest.mark.slow
def test_large_undelayed_upload_is_whole(app, ports):
    """A multi-megabyte undelayed transfer arrives whole and byte exact."""
    data = payload(LARGE_CB, seed=13)

    with SinkServer(ports.target) as sink:
        app.configure("127.0.0.1", ports.listen, "127.0.0.1", ports.target, 250)
        app.start()
        assert_started(app)

        with connect(ports.listen, timeout=180.0) as client:
            client.sendall(data)
            client.shutdown(socket.SHUT_WR)
            result = sink.wait_for_stream(timeout=180.0)

    assert result is not None, "the target never completed a receive"
    total, digest = result
    assert total == len(data), f"target got {total} bytes of {len(data)}"
    assert digest == digest_of(data), "target payload differs from what was sent"


def test_half_close_lets_the_reply_through(app, ports):
    """A client FIN reaches the target and the reply still returns in full.

    A relay that treats the first FIN as a full close truncates the response.
    This is the shape of that bug.
    """
    with EchoServer(ports.target, transform=lambda data: b"REPLY:" + data):
        app.configure("127.0.0.1", ports.listen, "127.0.0.1", ports.target, 300)
        app.start()
        assert_started(app)

        with connect(ports.listen) as client:
            client.sendall(b"question")
            client.shutdown(socket.SHUT_WR)
            chunks = []

            while True:
                part = client.recv(4096)

                if not part:
                    break

                chunks.append(part)

    assert b"".join(chunks) == b"REPLY:question"


@pytest.mark.slow
def test_concurrent_sessions_do_not_cross(app, ports):
    """Many simultaneous clients each get their own uncorrupted stream.

    Each client sends a differently seeded payload, so a relay that mixed two
    sessions' buffers would fail the digest rather than quietly pass.
    """
    client_count = 20
    results: list = [None] * client_count

    def run_client(index: int) -> None:
        """Sends one seeded payload and records whether it came back intact.

        :param index: Client number, used as the payload seed.
        """
        data = payload(32 * 1024, seed=100 + index)

        try:
            with connect(ports.listen, timeout=120.0) as client:
                client.sendall(data)
                client.shutdown(socket.SHUT_WR)
                total, digest = read_to_eof(client, timeout=120.0)

            if total != len(data):
                results[index] = f"got {total} bytes of {len(data)}"
            elif digest != digest_of(data):
                results[index] = "payload corrupted"
        except Exception as exc:  # pylint: disable=broad-except
            results[index] = repr(exc)

    with EchoServer(ports.target):
        app.configure("127.0.0.1", ports.listen, "127.0.0.1", ports.target, 150)
        app.start()
        assert_started(app)

        threads = [
            threading.Thread(target=run_client, args=(index,))
            for index in range(client_count)
        ]

        for thread in threads:
            thread.start()

        for thread in threads:
            thread.join(timeout=150)

    failures = {index: why for index, why in enumerate(results) if why}
    assert (
        not failures
    ), f"{len(failures)} of {client_count} sessions failed: {failures}"


@pytest.mark.slow
def test_slow_client_still_gets_every_byte(app, ports):
    """A client reading far slower than the target sends still gets it all.

    Keeps the relay's socket to the client full, so its sends keep failing
    with WSAEWOULDBLOCK and every chunk has to wait for a fresh
    FD_WRITE before it moves. That is the path RELAY_MAX_PUMP_WAIT_MS
    exists to protect: those notifications are edge triggered, and a pass
    that polls the socket without having earned one can consume it, leaving
    the session stalled until the peer gives up.

    A client that keeps up never fills the buffer, so none of the other
    transfers here exercise this at all.

    Note on what this does *not* cover: the resume-from-offset branch in
    DirectionWrite handles a send that completes short. Windows does not
    produce those for a non-blocking stream socket — WSASend either
    buffers the whole request or fails with WSAEWOULDBLOCK — so that
    branch is defensive and cannot be driven from a black box test here.
    """
    data = payload(1024 * 1024, seed=19)

    with SourceServer(ports.target, data):
        app.configure("127.0.0.1", ports.listen, "127.0.0.1", ports.target, 0)
        app.start()
        assert_started(app)

        client = socket.socket()
        # Before connect: the receive window is negotiated during the
        # handshake, so setting this afterwards would not shrink it.
        client.setsockopt(socket.SOL_SOCKET, socket.SO_RCVBUF, 8192)
        client.settimeout(180.0)
        client.connect(("127.0.0.1", ports.listen))

        digest = hashlib.sha256()
        total = 0
        started = time.monotonic()

        try:
            while True:
                chunk = client.recv(2048)

                if not chunk:
                    break

                digest.update(chunk)
                total += len(chunk)

                # Drain far below what the target is pushing, so the relay
                # sits against a full send buffer for the whole transfer.
                time.sleep(0.004)
        finally:
            client.close()

        elapsed = time.monotonic() - started

    assert total == len(data), f"received {total} bytes of {len(data)}"
    assert digest.hexdigest() == digest_of(data), "slow read corrupted the stream"
    # Proves the reader really was the bottleneck. If this transfer finished
    # quickly the throttle did not bite and the test proved nothing.
    assert elapsed > 1.0, (
        f"transfer took only {elapsed:.2f}s, so the relay was never held "
        "against a full send buffer and this test verified nothing"
    )
