"""Drives a running ``SnailNet.exe`` through its Win32 controls.

This module covers the GUI; :mod:`snailnet_tests.cli` covers headless mode.
It talks to the real window: it finds it by class name, sets
the edit fields, posts button clicks, and reads the status line back. The
control identifiers mirror ``SNAILNET_CTL_ID`` in ``SnailNet/Snailnet.h`` and
have to be kept in step with it.

.. rubric:: Why the window messages are sent by hand

``SetDlgItemTextW`` and ``GetDlgItemTextW`` look like the obvious way to do
this and they do not work from another process. ``SetWindowText`` across a
process boundary updates the window text USER32 caches for the window, while
the edit control keeps serving its own internal buffer to whoever asks it
properly. The effect is a test that reads back exactly what it wrote, is
satisfied, and drives an application still holding its default values.

Measured against a field whose default is ``8080``, asked to hold ``27703``:

``SetDlgItemTextW`` on the parent
    The control still reports ``8080``. Only the cache says ``27703``, so the
    application keeps using its default and the test never notices.

``WM_SETTEXT`` sent to the control
    The control reports ``27703``, which is what the application then reads.

So every read and write here addresses the control's handle directly with
``WM_GETTEXT`` and ``WM_SETTEXT``, which are marshalled across processes and
reach the control itself.

Reads use ``SendMessageTimeoutW`` rather than ``SendMessageW``, because a send
blocks until the target's UI thread picks the message up, and that thread
spends the whole of ``RelayStop`` blocked waiting on its workers. A plain
send would hang the test for exactly as long as the thing it is measuring.
"""

import ctypes
import ctypes.wintypes as wt
import logging
import subprocess
import time
from typing import Callable, Optional

#: Window class registered by ``RegisterMainClass`` in ``Ui.c``.
WINDOW_CLASS = "SnailNetWindow"

# SNAILNET_CTL_ID, Snailnet.h. These are a contract with the C source; a
# renumbering there silently breaks every test here, so they are named rather
# than inlined at the call sites.
IDC_LISTEN_ADDR = 101
IDC_LISTEN_PORT = 102
IDC_TARGET_ADDR = 103
IDC_TARGET_PORT = 104
IDC_LATENCY = 105
IDC_START = 106
IDC_STOP = 107
IDC_STATUS = 108

WM_CLOSE = 0x0010
WM_SETTEXT = 0x000C
WM_GETTEXT = 0x000D
WM_GETTEXTLENGTH = 0x000E
WM_COMMAND = 0x0111
BN_CLICKED = 0

#: Give up rather than wait out a UI thread that is not pumping messages.
SMTO_ABORTIFHUNG = 0x0002

#: Milliseconds allowed for a single text read.
READ_TIMEOUT_MS = 2000

_user32 = ctypes.WinDLL("user32", use_last_error=True)

_log = logging.getLogger(__name__)

#: Signature of the EnumWindows callback.
_ENUM_WINDOWS_PROC = ctypes.WINFUNCTYPE(wt.BOOL, wt.HWND, wt.LPARAM)

_user32.FindWindowW.restype = wt.HWND
_user32.FindWindowW.argtypes = [wt.LPCWSTR, wt.LPCWSTR]
_user32.EnumWindows.argtypes = [_ENUM_WINDOWS_PROC, wt.LPARAM]
_user32.GetClassNameW.argtypes = [wt.HWND, wt.LPWSTR, ctypes.c_int]
_user32.GetWindowThreadProcessId.argtypes = [wt.HWND, ctypes.POINTER(wt.DWORD)]
_user32.GetDlgItem.restype = wt.HWND
_user32.GetDlgItem.argtypes = [wt.HWND, ctypes.c_int]
_user32.SendMessageTimeoutW.restype = ctypes.c_longlong
_user32.SendMessageTimeoutW.argtypes = [
    wt.HWND,
    wt.UINT,
    wt.WPARAM,
    ctypes.c_void_p,
    wt.UINT,
    wt.UINT,
    ctypes.POINTER(ctypes.c_ulonglong),
]
_user32.PostMessageW.argtypes = [wt.HWND, wt.UINT, wt.WPARAM, ctypes.c_longlong]
_user32.IsWindow.argtypes = [wt.HWND]
_user32.IsWindowEnabled.argtypes = [wt.HWND]


class SnailNetError(RuntimeError):
    """Raised when the application cannot be launched or addressed."""


def _window_for_pid(pid: int) -> Optional[int]:
    """Finds the SnailNet window belonging to one process.

    :param pid: Process identifier to match.
    :return: The window handle, or None if that process has no window yet.
    """
    match: list = []

    def visit(hwnd: int, _lparam: int) -> bool:
        """Records ``hwnd`` when it is a SnailNet window owned by ``pid``."""
        name = ctypes.create_unicode_buffer(len(WINDOW_CLASS) + 2)
        _user32.GetClassNameW(hwnd, name, len(WINDOW_CLASS) + 2)

        if name.value != WINDOW_CLASS:
            return True

        owner = wt.DWORD()
        _user32.GetWindowThreadProcessId(hwnd, ctypes.byref(owner))

        if owner.value == pid:
            match.append(hwnd)
            return False

        return True

    _user32.EnumWindows(_ENUM_WINDOWS_PROC(visit), 0)

    return match[0] if match else None


class SnailNet:
    """One running instance of SnailNet, addressed through its window.

    Usable as a context manager, which is the expected form: the application
    holds a listening socket, so leaving one running past a test would make
    the next test fail on a port conflict rather than on its own merits.

    :param exe_path: Path to the built ``SnailNet.exe``.
    """

    def __init__(self, exe_path: str) -> None:
        """Records the executable path without launching anything yet."""
        self.exe_path = str(exe_path)
        self.proc: Optional[subprocess.Popen] = None
        self.hwnd: Optional[int] = None

    def __enter__(self) -> "SnailNet":
        """Launches the application and waits for its window."""
        return self.launch()

    def __exit__(self, *_exc_info: object) -> None:
        """Closes the application, killing it if it will not go quietly."""
        self.close()

    # -- lifecycle ---------------------------------------------------------

    def launch(self, timeout: float = 20.0) -> "SnailNet":
        """Starts the executable and waits until its controls exist.

        The window is matched to this instance's process id rather than taken
        from ``FindWindowW``, which returns whichever window of the class it
        happens to find first. A previous instance asked to close is still
        alive for as long as ``RelayStop`` takes to join its workers, so
        ``FindWindowW`` will hand back that dying window and every field
        written to it is lost when it exits.

        The window is also created before ``BuildControls`` has finished
        populating it, so this waits for the Start button to exist rather
        than for the window alone.

        :param timeout: Seconds to wait for the window to become usable.
        :raises SnailNetError: If no usable window appears in time.
        :return: This instance, for chaining.
        """
        # pylint: disable=consider-using-with
        self.proc = subprocess.Popen([self.exe_path])
        deadline = time.monotonic() + timeout

        while time.monotonic() < deadline:
            if self.proc.poll() is not None:
                raise SnailNetError(
                    f"SnailNet exited immediately with code {self.proc.returncode}"
                )

            hwnd = _window_for_pid(self.proc.pid)

            if hwnd and _user32.GetDlgItem(hwnd, IDC_START):
                self.hwnd = hwnd
                _log.info(
                    "launched SnailNet.exe (pid %s), window 0x%X", self.proc.pid, hwnd
                )
                # The fonts are applied right after the controls are created.
                # Settling here keeps the first field write from racing it.
                time.sleep(0.3)
                return self

            time.sleep(0.05)

        raise SnailNetError(f"SnailNet window did not appear within {timeout}s")

    def close(self, timeout: float = 20.0) -> None:
        """Asks the application to exit, then kills it if it does not.

        ``WM_CLOSE`` runs the real shutdown path, which blocks in ``RelayStop``
        until every worker thread is gone. That is worth exercising rather
        than short-circuiting with a kill, so the kill is only a fallback.

        :param timeout: Seconds to allow for an orderly exit.
        """
        if self.proc is None or self.proc.poll() is not None:
            return

        if self.hwnd and _user32.IsWindow(self.hwnd):
            _user32.PostMessageW(self.hwnd, WM_CLOSE, 0, 0)

            try:
                self.proc.wait(timeout=timeout)
            except subprocess.TimeoutExpired:
                pass

        if self.proc.poll() is None:
            self.proc.kill()
            self.proc.wait(timeout=5)

    @property
    def alive(self) -> bool:
        """True while the process is still running."""
        return self.proc is not None and self.proc.poll() is None

    # -- control text ------------------------------------------------------

    def _control(self, ctl_id: int) -> int:
        """Returns a child control's handle.

        :param ctl_id: Control identifier.
        :raises SnailNetError: If the control does not exist.
        :return: The control's window handle.
        """
        hctl = _user32.GetDlgItem(self.hwnd, ctl_id)

        if not hctl:
            raise SnailNetError(f"control {ctl_id} not found on window {self.hwnd}")

        return hctl

    def _set_text(self, ctl_id: int, text: str) -> None:
        """Sets a control's text so the owning process sees the change.

        :param ctl_id: Control identifier.
        :param text: Replacement text.
        """
        buf = ctypes.create_unicode_buffer(text)
        _user32.SendMessageTimeoutW(
            self._control(ctl_id),
            WM_SETTEXT,
            0,
            ctypes.cast(buf, ctypes.c_void_p),
            SMTO_ABORTIFHUNG,
            READ_TIMEOUT_MS,
            None,
        )

    def _get_text(self, ctl_id: int) -> str:
        """Reads a control's text, returning empty if the app is not pumping.

        :param ctl_id: Control identifier.
        :return: The control's text, or an empty string on timeout.
        """
        hctl = self._control(ctl_id)
        length = ctypes.c_ulonglong(0)

        if not _user32.SendMessageTimeoutW(
            hctl,
            WM_GETTEXTLENGTH,
            0,
            None,
            SMTO_ABORTIFHUNG,
            READ_TIMEOUT_MS,
            ctypes.byref(length),
        ):
            return ""

        size = int(length.value) + 1
        buf = ctypes.create_unicode_buffer(size + 1)

        if not _user32.SendMessageTimeoutW(
            hctl,
            WM_GETTEXT,
            size,
            ctypes.cast(buf, ctypes.c_void_p),
            SMTO_ABORTIFHUNG,
            READ_TIMEOUT_MS,
            None,
        ):
            return ""

        return buf.value

    # -- form --------------------------------------------------------------

    def configure(
        self,
        listen_addr: str,
        listen_port: object,
        target_addr: str,
        target_port: object,
        latency_ms: object,
    ) -> None:
        """Fills in all five relay fields.

        Values are stringified rather than validated, because rejecting bad
        input is the application's job and several tests exist to prove it
        does so.

        :param listen_addr: Address SnailNet binds.
        :param listen_port: Port SnailNet binds.
        :param target_addr: Address SnailNet forwards to.
        :param target_port: Port SnailNet forwards to.
        :param latency_ms: Delay applied on the way back to the client.
        """
        _log.info(
            "configure listen %s:%s target %s:%s latency %s",
            listen_addr,
            listen_port,
            target_addr,
            target_port,
            latency_ms,
        )
        self._set_text(IDC_LISTEN_ADDR, str(listen_addr))
        self._set_text(IDC_LISTEN_PORT, str(listen_port))
        self._set_text(IDC_TARGET_ADDR, str(target_addr))
        self._set_text(IDC_TARGET_PORT, str(target_port))
        self._set_text(IDC_LATENCY, str(latency_ms))

    def _click(self, ctl_id: int) -> None:
        """Posts the ``WM_COMMAND`` a real button click would send.

        Posted rather than sent: ``OnStop`` blocks inside ``RelayStop`` until
        the last session thread exits, and a send would block this process
        alongside it.

        :param ctl_id: Control identifier of the button.
        """
        hctl = _user32.GetDlgItem(self.hwnd, ctl_id)
        wparam = (BN_CLICKED << 16) | ctl_id
        _user32.PostMessageW(self.hwnd, WM_COMMAND, wparam, hctl or 0)

    def start(self) -> None:
        """Presses Start. Returns at once; the relay may not be up yet."""
        _log.info("pressed Start")
        self._click(IDC_START)

    def stop(self) -> None:
        """Presses Stop. Returns at once; the teardown may still be running."""
        _log.info("pressed Stop")
        self._click(IDC_STOP)

    # -- state -------------------------------------------------------------

    def status(self) -> str:
        """Returns the current status text, newlines and all."""
        return self._get_text(IDC_STATUS)

    def status_line(self) -> str:
        """Returns the status text flattened to one line, for assertions.

        :return: Status text with line breaks collapsed to ``" | "``.
        """
        parts = [part.strip() for part in self.status().splitlines() if part.strip()]
        return " | ".join(parts)

    @property
    def running(self) -> bool:
        """True when the relay is active.

        Read from whether Stop is enabled, because that is how the
        application itself reports its state through ``SetRunning``. This
        sends no message, so it stays answerable while the UI thread is
        blocked inside ``RelayStop``.
        """
        hstop = _user32.GetDlgItem(self.hwnd, IDC_STOP)
        return bool(hstop) and bool(_user32.IsWindowEnabled(hstop))

    def wait_running(self, timeout: float = 15.0) -> bool:
        """Waits for the relay to report that it is listening.

        :param timeout: Seconds to wait.
        :return: True if it started, False on timeout.
        """
        return self._wait_for(lambda: self.running, timeout)

    def wait_idle(self, timeout: float = 40.0) -> bool:
        """Waits for the relay to report that it has stopped.

        :param timeout: Seconds to wait.
        :return: True if it went idle, False on timeout.
        """
        return self._wait_for(lambda: not self.running, timeout)

    def wait_status(
        self, predicate: Callable[[str], bool], timeout: float = 15.0
    ) -> str:
        """Waits for the status text to satisfy a predicate.

        :param predicate: Called with the flattened status text on each poll.
        :param timeout: Seconds to wait.
        :return: The last status text seen, matched or not.
        """
        deadline = time.monotonic() + timeout
        text = self.status_line()

        while time.monotonic() < deadline:
            text = self.status_line()

            if predicate(text):
                return text

            time.sleep(0.05)

        return text

    @staticmethod
    def _wait_for(predicate: Callable[[], bool], timeout: float) -> bool:
        """Polls a predicate until it holds or the timeout expires.

        :param predicate: Condition to test.
        :param timeout: Seconds to wait.
        :return: True if the predicate held in time.
        """
        deadline = time.monotonic() + timeout

        while time.monotonic() < deadline:
            if predicate():
                return True

            time.sleep(0.05)

        return False
