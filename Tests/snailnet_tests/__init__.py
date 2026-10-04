"""End to end test support for SnailNet.

SnailNet is a Win32 GUI application with no command line, so the only honest
way to test the binary that ships is to drive it the way a person would: fill
in its fields, press its buttons, and watch real TCP traffic come out the far
side. That is what this package provides.

:mod:`snailnet_tests.app`
    Drives one running ``SnailNet.exe`` through its window handle.

:mod:`snailnet_tests.servers`
    Throwaway TCP servers that stand in for whatever the relay forwards to.

:mod:`snailnet_tests.support`
    Port allocation, payload generation, and stream readers.

Nothing here stubs the relay or relinks its object files. A test that passes
here passed against the executable.
"""

from snailnet_tests.app import SnailNet
from snailnet_tests.servers import EchoServer, SinkServer, SourceServer
from snailnet_tests.support import digest_of, free_port, payload, read_to_eof

__all__ = [
    "EchoServer",
    "SinkServer",
    "SnailNet",
    "SourceServer",
    "digest_of",
    "free_port",
    "payload",
    "read_to_eof",
]
