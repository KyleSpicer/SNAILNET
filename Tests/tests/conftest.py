"""Shared fixtures and command line options for the SnailNet suite."""

import pathlib
import subprocess
import sys
from typing import Iterator, NamedTuple

import pytest

from snailnet_tests import SnailNet, free_port

#: Repository root, two levels up from this file (Tests/tests/conftest.py).
REPO_ROOT = pathlib.Path(__file__).resolve().parents[2]

#: Where MSBuild puts the executable, per OutDir in SnailNet.vcxproj.
DEFAULT_EXE = REPO_ROOT / "SnailNet" / "Build" / "x64" / "Release" / "SnailNet.exe"


def pytest_addoption(parser: pytest.Parser) -> None:
    """Registers the suite's command line options.

    :param parser: Parser supplied by pytest.
    """
    parser.addoption(
        "--exe",
        action="store",
        default=str(DEFAULT_EXE),
        help="Path to the SnailNet.exe under test.",
    )


@pytest.fixture(name="exe_path", scope="session")
def exe_path_fixture(pytestconfig: pytest.Config) -> str:
    """Path to the executable under test.

    :param pytestconfig: Active pytest configuration.
    :raises pytest.UsageError: If the executable is missing, since every test
        in the suite would otherwise fail with the same unhelpful error.
    :return: Absolute path to the executable.
    """
    path = pathlib.Path(pytestconfig.getoption("--exe")).resolve()

    if not path.is_file():
        raise pytest.UsageError(
            f"SnailNet.exe not found at {path}\n"
            "Build it first, for example:\n"
            '  & "$(vswhere -latest -property installationPath)'
            '\\MSBuild\\Current\\Bin\\MSBuild.exe" '
            "SnailNet\\SnailNet.slnx /p:Configuration=Release /p:Platform=x64\n"
            "or point the suite at another build with --exe PATH"
        )

    return str(path)


@pytest.fixture(scope="session", autouse=True)
def _no_stray_instances() -> Iterator[None]:
    """Kills leftover SnailNet processes around the session.

    A SnailNet left running from an interrupted session still holds its
    listening port, which would fail the next run for a reason that has
    nothing to do with the code under test.
    """
    _kill_strays()
    yield
    _kill_strays()


def _kill_strays() -> None:
    """Terminates any running SnailNet.exe, ignoring the common failure."""
    if sys.platform != "win32":
        return

    subprocess.run(
        ["taskkill", "/F", "/IM", "SnailNet.exe"],
        capture_output=True,
        check=False,
    )


class Ports(NamedTuple):
    """A listen port for the relay and a target port for the test server."""

    listen: int
    target: int


@pytest.fixture(name="ports")
def ports_fixture() -> Ports:
    """Two ports that were free a moment ago.

    :return: The listen and target ports.
    """
    return Ports(listen=free_port(), target=free_port())


@pytest.fixture(name="app")
def app_fixture(exe_path: str) -> Iterator[SnailNet]:
    """A launched SnailNet, closed again when the test finishes.

    :param exe_path: Executable under test.
    :return: The running application.
    """
    with SnailNet(exe_path) as instance:
        yield instance


def assert_started(app: SnailNet) -> None:
    """Fails with the status line when the relay did not start.

    The status line carries the actual reason — a bind conflict, an
    unresolvable target — so a bare "did not start" wastes the one piece of
    diagnostic the application offers.

    :param app: The application that was asked to start.
    """
    if not app.wait_running():
        pytest.fail(f"relay never started; status reads {app.status_line()!r}")
