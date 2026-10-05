"""Drives SnailNet in headless command line mode.

Unlike :class:`snailnet_tests.app.SnailNet`, which drives the GUI through
its window, this launches the executable with command line arguments and
reads its stdout, stderr, and exit code directly: there is no window to
find or wait for, and shutdown is a Ctrl+C, not a button.
"""

import ctypes
import subprocess
from typing import List, Optional, Tuple

#: Lets the child own its own console process group, which
#: GenerateConsoleCtrlEvent needs in order to target it specifically
#: rather than every process sharing a console with this one.
CREATE_NEW_PROCESS_GROUP = 0x00000200

#: Value GenerateConsoleCtrlEvent expects for a Ctrl+C.
CTRL_C_EVENT = 0

_kernel32 = ctypes.WinDLL("kernel32", use_last_error=True)


def has_console() -> bool:
    """Reports whether this process has a real console attached.

    GenerateConsoleCtrlEvent is a no-op, not an error, when the calling
    process has none: Windows does not raise anything, the call simply
    has nothing to deliver. A process launched by a harness that pipes
    or otherwise detaches standard handles can end up with no console at
    all, which is exactly the situation this exists to detect so a test
    that depends on sending a real Ctrl+C can skip instead of hanging.

    :return: True if GetConsoleWindow found one.
    """
    return bool(_kernel32.GetConsoleWindow())


class CliProcess:
    """One running ``SnailNet.exe`` launched in headless mode.

    Usable as a context manager; exiting kills the process if it is
    still running, the same reasoning as :class:`snailnet_tests.app.SnailNet`
    for not leaving one behind to hold a port past its test.

    :param exe_path: Path to the built executable.
    :param args: Command line arguments, excluding the executable itself.
    """

    def __init__(self, exe_path: str, args: List[str]) -> None:
        """Records the executable and arguments without launching yet."""
        self.exe_path = str(exe_path)
        self.args = list(args)
        self.proc: Optional[subprocess.Popen] = None

    def __enter__(self) -> "CliProcess":
        """Starts the process."""
        return self.start()

    def __exit__(self, *_exc_info: object) -> None:
        """Kills the process if it is still running."""
        self.stop()

    def start(self) -> "CliProcess":
        """Launches the process in its own console process group.

        :return: This instance, for chaining.
        """
        # pylint: disable=consider-using-with
        self.proc = subprocess.Popen(
            [self.exe_path, *self.args],
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            text=True,
            creationflags=CREATE_NEW_PROCESS_GROUP,
        )
        return self

    @property
    def alive(self) -> bool:
        """True while the process is still running."""
        return self.proc is not None and self.proc.poll() is None

    def send_ctrl_c(self) -> None:
        """Sends a real CTRL_C_EVENT to the process's console group.

        Meaningful only when this process itself has a console; see
        :func:`has_console`. Without one, Windows has nothing to deliver
        the signal through and the call does nothing observable.
        """
        _kernel32.GenerateConsoleCtrlEvent(CTRL_C_EVENT, self.proc.pid)

    def wait(self, timeout: float = 20.0) -> Tuple[int, str, str]:
        """Waits for the process to exit and collects its output.

        :param timeout: Seconds to wait.
        :raises subprocess.TimeoutExpired: If it does not exit in time.
        :return: Exit code, stdout, and stderr.
        """
        stdout, stderr = self.proc.communicate(timeout=timeout)
        return self.proc.returncode, stdout, stderr

    def stop(self) -> None:
        """Kills the process if it is still running."""
        if self.alive:
            self.proc.kill()
            self.proc.wait(timeout=5)
