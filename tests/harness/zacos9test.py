"""What ZacOS's Python tests share. Every test run by `meson test` has
tests/harness on PYTHONPATH (the root meson.build's test setup), so a test
says `from zacos9test import wait_for`. Run by hand, add it first:

    PYTHONPATH=tests/harness python3 tests/compositor/test_idle.py ...
"""
import socket
import time


def wait_for(what, check, seconds=15, interval=0.05):
    """check()'s first true value, asking every `interval` seconds; fails
    the test, naming `what`, if there's none within `seconds`."""
    deadline = time.monotonic() + seconds
    while time.monotonic() < deadline:
        value = check()
        if value:
            return value
        time.sleep(interval)
    raise AssertionError(f"timed out waiting for {what}")


def free_port():
    """A TCP port on 127.0.0.1 nothing is listening on, for a test's server."""
    with socket.socket() as s:
        s.bind(("127.0.0.1", 0))
        return s.getsockname()[1]
