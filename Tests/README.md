# SnailNet Test Suite

End to end tests that drive the real `SnailNet.exe`.

SnailNet ignores its command line, so there is no headless mode to target.
This suite finds the window by class name, fills in the five relay fields,
posts button clicks, and reads the status line back, then moves live TCP
traffic through the relay and checks it on the far side. Nothing is stubbed
and no object files are relinked.

For a walkthrough aimed at someone who has just cloned the repository, see
[`../Docs/TestGuide.md`](../Docs/TestGuide.md).

## Quick start

```console
cd Tests
poetry install
poetry run pytest
```

The suite needs a built executable. It defaults to the x64 Release build at
`SnailNet/Build/x64/Release/SnailNet.exe`; use `--exe` to point elsewhere.

```console
poetry run pytest -m "not slow"          # skip the multi-megabyte transfers
poetry run pytest tests/test_lifecycle.py
poetry run pytest --exe ..\SnailNet\Build\x64\Debug\SnailNet.exe
```

## Test reports

Every run writes a JUnit XML report to `reports/junit.xml`, picked up
automatically via `addopts` in `pyproject.toml`; no extra flag needed. It is
a build artifact, gitignored, and overwritten on each run. This is the format
a CI step would parse; for a human, the console output above is the more
readable record.

## Layout

| Path | What it holds |
|---|---|
| `snailnet_tests/app.py` | Drives one running instance through its window handle |
| `snailnet_tests/servers.py` | Echo, source, and sink servers to relay against |
| `snailnet_tests/support.py` | Ports, payloads, stream readers |
| `tests/test_correctness.py` | Stream integrity and latency |
| `tests/test_lifecycle.py` | Start, stop, restart, failure reporting |
| `tests/test_validation.py` | Bad form input is refused |
| `docs/` | Sphinx autodoc sources |

## Documentation

```console
poetry install --with docs
poetry run sphinx-build -b html docs docs/_build/html
```

Then open `docs/_build/html/index.html`.

## Note on control identifiers

`snailnet_tests/app.py` hard codes the control identifiers from
`SNAILNET_CTL_ID` in `SnailNet/Snailnet.h`. That enum is a contract with this
suite: renumbering it silently breaks every test here, because the tests
would be typing into controls that no longer exist.
