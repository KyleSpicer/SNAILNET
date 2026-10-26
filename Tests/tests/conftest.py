"""Shared fixtures and command line options for the SnailNet suite."""

import collections
import datetime
import html
import pathlib
import subprocess
import sys
import textwrap
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
    parser.addoption(
        "--report",
        action="store",
        default=None,
        metavar="PATH",
        help="Write a static HTML test report to PATH.",
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

    The status line carries the actual reason (a bind conflict, an
    unresolvable target), so a bare "did not start" wastes the one piece of
    diagnostic the application offers.

    :param app: The application that was asked to start.
    """
    if not app.wait_running():
        pytest.fail(f"relay never started; status reads {app.status_line()!r}")


# ---------------------------------------------------------------------------
# Static HTML test report, written when --report PATH is given.
#
# One row per test: outcome, name, what it checks (its docstring), duration,
# and an expandable section with the commands it ran (the harness logs them)
# plus any failure or skip output. Plain HTML with <details>: no JavaScript,
# so it renders in any viewer, including ones that do not run scripts.
# ---------------------------------------------------------------------------

#: One dict per test, in run order, filled by pytest_runtest_logreport.
_results: list = []

_OUTCOME_COLORS = {
    "passed": "#1a7f37",
    "failed": "#cf222e",
    "error": "#cf222e",
    "skipped": "#9a6700",
}

_REPORT_PAGE = """<!doctype html>
<html lang="en"><head><meta charset="utf-8">
<title>SnailNet test report</title>
<style>
body {{ font-family: Segoe UI, sans-serif; margin: 24px; color: #1f2328; }}
table {{ border-collapse: collapse; width: 100%; }}
th, td {{ border: 1px solid #d0d7de; padding: 8px; text-align: left;
         vertical-align: top; }}
th {{ background: #f6f8fa; }}
pre {{ background: #f6f8fa; padding: 8px; white-space: pre-wrap; }}
summary {{ cursor: pointer; color: #0969da; margin-top: 6px; }}
</style></head><body>
<h1>SnailNet test report</h1>
<p>{summary}<br>Executable: <code>{exe}</code><br>Generated {when}</p>
<table><tr><th>Result</th><th>Test</th><th>Duration</th></tr>
{rows}
</table></body></html>
"""


def _description_of(item: pytest.Item) -> str:
    """Returns a test's docstring prose, dedented, for the report.

    The ``:param:``/``:return:``/``:raises:`` field lines are dropped: they
    document the test function for a developer, and are noise in a report
    that is meant to say what the test checks.

    :param item: The test item.
    :return: The docstring's prose, or an empty string.
    """
    doc = getattr(getattr(item, "function", None), "__doc__", None)

    if not doc:
        return ""

    lines = [
        line
        for line in textwrap.dedent(doc).splitlines()
        if not line.lstrip().startswith(":")
    ]
    return "\n".join(lines).strip()


@pytest.hookimpl(hookwrapper=True)
def pytest_runtest_makereport(item: pytest.Item):
    """Attaches the test's description to its report for the HTML report.

    Declares only ``item``; pluggy passes a hook impl just the arguments it
    names, so the unused ``call`` is left off.
    """
    outcome = yield
    report = outcome.get_result()
    report.description = _description_of(item)


def _details_of(report: pytest.TestReport) -> str:
    """Collects what a test did and why it ended as it did.

    :param report: The phase report that decided the test's outcome.
    :return: Captured log (the commands the harness ran), captured output,
        and the failure or skip reason, or an empty string.
    """
    parts = [
        report.caplog,
        f"stdout:\n{report.capstdout}" if report.capstdout else "",
        f"stderr:\n{report.capstderr}" if report.capstderr else "",
    ]

    if report.skipped and isinstance(report.longrepr, tuple):
        parts.append(report.longrepr[2])
    elif report.failed:
        parts.append(report.longreprtext)

    return "\n\n".join(part.strip() for part in parts if part and part.strip())


def pytest_runtest_logreport(report: pytest.TestReport) -> None:
    """Records one report row per test.

    The call phase decides a normal test. A test skipped or broken during
    setup never reaches call, and a teardown failure is reported as well.
    """
    decides = report.when == "call" or (
        report.when == "setup" and report.outcome != "passed"
    )

    if not (decides or (report.when == "teardown" and report.failed)):
        return

    outcome = report.outcome
    if report.failed and report.when != "call":
        outcome = "error"

    _results.append(
        {
            "name": report.nodeid,
            "outcome": outcome,
            "description": getattr(report, "description", ""),
            "duration": report.duration,
            "details": _details_of(report),
        }
    )


def _row_html(row: dict) -> str:
    """Renders one test as a table row.

    :param row: A dict recorded by pytest_runtest_logreport.
    :return: The row's HTML.
    """
    color = _OUTCOME_COLORS.get(row["outcome"], "#57606a")
    description = html.escape(row["description"]).replace("\n", "<br>")
    details = ""

    if row["details"]:
        details = (
            "<details><summary>Commands and output</summary>"
            f"<pre>{html.escape(row['details'])}</pre></details>"
        )

    return (
        f'<tr><td style="color:{color}"><b>{row["outcome"].title()}</b></td>'
        f"<td><code>{html.escape(row['name'])}</code><br>{description}{details}</td>"
        f"<td>{row['duration']:.2f}s</td></tr>"
    )


def pytest_sessionfinish(session: pytest.Session) -> None:
    """Writes the static report, when --report was given.

    :param session: The finished test session.
    """
    path = session.config.getoption("--report")

    if not path:
        return

    counts = collections.Counter(row["outcome"] for row in _results)
    summary = ", ".join(f"{count} {name}" for name, count in sorted(counts.items()))
    page = _REPORT_PAGE.format(
        summary=html.escape(summary or "no tests ran"),
        exe=html.escape(session.config.getoption("--exe")),
        when=datetime.datetime.now().strftime("%Y-%m-%d %H:%M:%S"),
        rows="\n".join(_row_html(row) for row in _results),
    )

    out = pathlib.Path(path)
    out.parent.mkdir(parents=True, exist_ok=True)
    out.write_text(page, encoding="utf-8")
