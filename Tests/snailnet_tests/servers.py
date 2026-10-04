"""Throwaway TCP servers that stand in for whatever the relay forwards to.

Three shapes cover what the relay needs proving against: one that replies
(:class:`EchoServer`), one that pushes (:class:`SourceServer`), and one that
swallows (:class:`SinkServer`). Each is a context manager that binds on
construction and stops accepting on exit.

The reply direction is the delayed one, so :class:`EchoServer` records when
each connection's first byte arrived. That timestamp is what lets a test
prove the delay is one way rather than merely present.
"""

import hashlib
import socket
import threading
import time
from typing import Callable, List, Optional, Tuple

#: How long an accept loop blocks before re-checking its stop flag.
ACCEPT_SLICE_S = 0.25


class _ServerBase:
    """Common bind, accept loop, and shutdown for the test servers.

    :param port: Loopback port to bind.
    :param backlog: Listen backlog.
    """

    def __init__(self, port: int, backlog: int = 128) -> None:
        """Binds and listens immediately, so the port is held on return."""
        self.port = port
        self._stop = threading.Event()
        self._sock = socket.socket()
        self._sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        self._sock.bind(("127.0.0.1", port))
        self._sock.listen(backlog)
        self._accept_thread = threading.Thread(target=self._accept_loop, daemon=True)

    def __enter__(self):
        """Starts accepting connections."""
        self._accept_thread.start()
        return self

    def __exit__(self, *_exc_info: object) -> None:
        """Stops accepting and releases the listening socket."""
        self.close()

    def close(self) -> None:
        """Stops the accept loop and closes the listening socket."""
        self._stop.set()

        try:
            self._sock.close()
        except OSError:
            pass

    def _accept_loop(self) -> None:
        """Accepts until closed, handing each connection to a worker."""
        self._sock.settimeout(ACCEPT_SLICE_S)

        while not self._stop.is_set():
            try:
                conn, _ = self._sock.accept()
            except socket.timeout:
                continue
            except OSError:
                break

            threading.Thread(target=self._serve, args=(conn,), daemon=True).start()

    def _serve(self, conn: socket.socket) -> None:
        """Handles one connection. Overridden by each subclass.

        :param conn: The accepted socket.
        """
        raise NotImplementedError


class EchoServer(_ServerBase):
    """Reads and writes back, recording when each stream first arrived.

    :param port: Loopback port to bind.
    :param transform: Optional callable applied to each buffer before it is
        echoed, for tests that need the reply to differ from the request.
    """

    def __init__(
        self,
        port: int,
        transform: Optional[Callable[[bytes], bytes]] = None,
    ) -> None:
        """Binds the port and prepares the arrival-time record."""
        super().__init__(port)
        self._transform = transform
        self._lock = threading.Lock()
        self.first_byte_at: List[float] = []

    def _serve(self, conn: socket.socket) -> None:
        """Echoes until the peer half-closes, then half-closes in turn.

        :param conn: The accepted socket.
        """
        with conn:
            conn.settimeout(60.0)
            stamped = False

            try:
                while True:
                    data = conn.recv(65536)

                    if not data:
                        break

                    if not stamped:
                        with self._lock:
                            self.first_byte_at.append(time.monotonic())

                        stamped = True

                    conn.sendall(self._transform(data) if self._transform else data)
            except OSError:
                pass

            # Half close rather than a bare close, so the relay sees an
            # orderly FIN and the client is not racing a reset.
            try:
                conn.shutdown(socket.SHUT_WR)
            except OSError:
                pass


class SourceServer(_ServerBase):
    """Pushes a fixed payload at every client, then half-closes.

    Used for the delayed direction, where the relay has to hold megabytes in
    its queue and release them on schedule without dropping any.

    :param port: Loopback port to bind.
    :param data: Payload sent to each client.
    """

    def __init__(self, port: int, data: bytes) -> None:
        """Binds the port and stores the payload to serve."""
        super().__init__(port)
        self._data = data

    def _serve(self, conn: socket.socket) -> None:
        """Sends the whole payload, half-closes, then stays open.

        Staying open past the send matters: the relay may still be draining
        its delay queue toward the client, and a full close here would send a
        reset that destroys the very bytes the test is counting.

        :param conn: The accepted socket.
        """
        with conn:
            try:
                conn.settimeout(120.0)
                conn.sendall(self._data)
                conn.shutdown(socket.SHUT_WR)

                while conn.recv(65536):
                    pass
            except OSError:
                pass


class SinkServer(_ServerBase):
    """Swallows everything each client sends, recording size and digest.

    Used for the undelayed direction, to prove an upload arrives whole.

    :param port: Loopback port to bind.
    """

    def __init__(self, port: int) -> None:
        """Binds the port and prepares the results list."""
        super().__init__(port)
        self._lock = threading.Lock()
        self.received: List[Tuple[int, str]] = []

    def _serve(self, conn: socket.socket) -> None:
        """Drains the connection to EOF and records what arrived.

        :param conn: The accepted socket.
        """
        digest = hashlib.sha256()
        total = 0

        with conn:
            conn.settimeout(120.0)

            try:
                while True:
                    data = conn.recv(262144)

                    if not data:
                        break

                    digest.update(data)
                    total += len(data)
            except OSError:
                pass

        with self._lock:
            self.received.append((total, digest.hexdigest()))

    def wait_for_stream(self, timeout: float = 90.0) -> Optional[Tuple[int, str]]:
        """Waits for one client's stream to finish and returns its result.

        :param timeout: Seconds to wait.
        :return: Byte count and digest, or None on timeout.
        """
        deadline = time.monotonic() + timeout

        while time.monotonic() < deadline:
            with self._lock:
                if self.received:
                    return self.received[0]

            time.sleep(0.1)

        return None
