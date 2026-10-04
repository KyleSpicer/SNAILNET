SnailNet Test Suite
===================

End to end tests for `SnailNet <https://gitea.com/4-Projects/SNAILNET>`_, a
Win32 TCP relay that injects configurable latency into a connection.

SnailNet ignores its command line, so there is no headless mode to target.
This suite drives the real executable instead: it finds the window by class
name, fills in the five relay fields, posts button clicks, and reads the
status line back. Live TCP traffic then moves through the relay and is
checked on the far side.

Nothing is stubbed and no object files are relinked. A test that passes here
passed against the binary that ships.

Running the suite
-----------------

.. code-block:: console

   cd Tests
   poetry install
   poetry run pytest

The suite needs a built executable. By default it looks for the x64 Release
build at ``SnailNet/Build/x64/Release/SnailNet.exe``; point it elsewhere with
``--exe``:

.. code-block:: console

   poetry run pytest --exe ..\SnailNet\Build\x64\Debug\SnailNet.exe

Tests marked ``slow`` move multiple megabytes or wait out a long delay. Skip
them for a quick pass:

.. code-block:: console

   poetry run pytest -m "not slow"

What is covered
---------------

**Correctness** — byte-exact round trips; the delay applying one way only,
measured in both directions; multi-megabyte transfers past the 1 MB queue
cap in each direction; half close propagation; concurrent sessions not
crossing streams.

**Lifecycle** — Stop returning to idle and genuinely releasing the port;
repeated start and stop on the same port; Stop with a transfer in flight; a
dead target and a bind conflict both being reported.

**Validation** — out of range and non-numeric ports, bad and negative
latency, blank and unresolvable addresses, plus the boundary cases that a
blanket refusal would otherwise hide.

Building these pages
--------------------

.. code-block:: console

   poetry install --with docs
   poetry run sphinx-build -b html docs docs/_build/html

API reference
-------------

.. toctree::
   :maxdepth: 2

   api
