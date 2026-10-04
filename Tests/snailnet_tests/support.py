"""Port allocation, payload generation, and stream readers."""

import hashlib
import socket
from typing import Tuple


def free_port() -> int:
    """Returns a loopback port that was free a moment ago.

    Binding to port zero and reading back what the stack assigned is
    inherently a little racy, but it beats hard coding ports that collide
    with whatever else the machine happens to be running.

    :return: A port number.
    """
    with socket.socket() as sock:
        sock.bind(("127.0.0.1", 0))
        return int(sock.getsockname()[1])


def payload(size: int, seed: int = 1) -> bytes:
    """Builds deterministic pseudorandom bytes.

    A repeating or low-entropy pattern can hide a truncation or a reordering,
    because the corrupted stream still compares equal in the damaged region.
    These bytes are reproducible from the seed but have no short period, so a
    digest mismatch means real damage.

    :param size: Number of bytes to produce.
    :param seed: Seed, so a failing case can be reproduced exactly.
    :return: Exactly ``size`` bytes.
    """
    out = bytearray()
    state = seed & 0xFFFFFFFF

    while len(out) < size:
        state = (1103515245 * state + 12345) & 0xFFFFFFFF
        out += state.to_bytes(4, "little")

    return bytes(out[:size])


def digest_of(data: bytes) -> str:
    """Returns the hex SHA-256 of a buffer.

    :param data: Bytes to hash.
    :return: Lowercase hex digest.
    """
    return hashlib.sha256(data).hexdigest()


def read_to_eof(sock: socket.socket, timeout: float = 90.0) -> Tuple[int, str]:
    """Reads a socket until the peer closes, hashing as it goes.

    The payload is hashed rather than accumulated so a multi-megabyte
    transfer does not need to be held in memory twice over.

    :param sock: Connected socket to drain.
    :param timeout: Seconds allowed between reads.
    :raises TimeoutError: If a read stalls past the timeout, reporting how
        far the transfer got, which is the useful part when a relay truncates.
    :return: Byte count and hex digest.
    """
    sock.settimeout(timeout)
    digest = hashlib.sha256()
    total = 0

    while True:
        try:
            data = sock.recv(262144)
        except socket.timeout as exc:
            raise TimeoutError(f"stream stalled after {total} bytes") from exc

        if not data:
            break

        digest.update(data)
        total += len(data)

    return total, digest.hexdigest()


def connect(port: int, timeout: float = 30.0) -> socket.socket:
    """Opens a loopback connection with a timeout already applied.

    :param port: Port to connect to on 127.0.0.1.
    :param timeout: Seconds for both the connect and subsequent operations.
    :return: The connected socket.
    """
    sock = socket.create_connection(("127.0.0.1", port), timeout=timeout)
    sock.settimeout(timeout)
    return sock
