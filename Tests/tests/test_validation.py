"""Input validation: bad form values are refused rather than acted on.

Every case here has to leave the application idle. A relay that starts on a
port of zero or an unresolvable target has not validated anything; it has
only deferred the failure to somewhere less visible.
"""

import time

import pytest

#: (label, listen_addr, listen_port, target_addr, target_port, latency)
REJECTED_INPUTS = [
    ("port below range", "127.0.0.1", "0", "127.0.0.1", "9000", "0"),
    ("port above range", "127.0.0.1", "65536", "127.0.0.1", "9000", "0"),
    ("port far above range", "127.0.0.1", "999999", "127.0.0.1", "9000", "0"),
    ("non numeric port", "127.0.0.1", "abc", "127.0.0.1", "9000", "0"),
    ("empty port", "127.0.0.1", "", "127.0.0.1", "9000", "0"),
    ("target port below range", "127.0.0.1", "9999", "127.0.0.1", "0", "0"),
    ("target port above range", "127.0.0.1", "9999", "127.0.0.1", "65536", "0"),
    ("blank listen address", "", "9999", "127.0.0.1", "9000", "0"),
    ("blank target address", "127.0.0.1", "9999", "", "9000", "0"),
    ("non numeric latency", "127.0.0.1", "9999", "127.0.0.1", "9000", "soon"),
    ("negative latency", "127.0.0.1", "9999", "127.0.0.1", "9000", "-5"),
    ("empty latency", "127.0.0.1", "9999", "127.0.0.1", "9000", ""),
    (
        "unresolvable target",
        "127.0.0.1",
        "9999",
        "no.such.host.invalid",
        "9000",
        "0",
    ),
    (
        "unbindable listen address",
        "203.0.113.1",
        "9999",
        "127.0.0.1",
        "9000",
        "0",
    ),
]


@pytest.mark.parametrize(
    "label,listen_addr,listen_port,target_addr,target_port,latency",
    REJECTED_INPUTS,
    ids=[case[0].replace(" ", "_") for case in REJECTED_INPUTS],
)
def test_bad_input_is_refused(
    app,
    label,
    listen_addr,
    listen_port,
    target_addr,
    target_port,
    latency,
):
    """Pressing Start on invalid input leaves the relay idle.

    :param app: The running application.
    :param label: Human readable description of the case.
    :param listen_addr: Listen address to type in.
    :param listen_port: Listen port to type in.
    :param target_addr: Target address to type in.
    :param target_port: Target port to type in.
    :param latency: Latency to type in.
    """
    app.configure(listen_addr, listen_port, target_addr, target_port, latency)
    app.start()

    # Long enough for an address resolution attempt to come back, which is
    # the slowest of these paths.
    time.sleep(1.0)

    assert not app.running, (
        f"{label}: the relay started on input it should have refused; "
        f"status {app.status_line()!r}"
    )
    assert app.alive, f"{label}: the application died"
    assert app.status_line(), f"{label}: nothing was reported on the status line"


def test_valid_boundary_ports_are_accepted(app, ports):
    """Port 65535 is accepted, so the range check is not off by one.

    The rejection tests above would also pass against an implementation that
    refused every port, so the upper boundary needs its own case.
    """
    app.configure("127.0.0.1", 65535, "127.0.0.1", ports.target, 0)
    app.start()

    started = app.wait_running(timeout=8)
    status = app.status_line()

    if started:
        app.stop()
        app.wait_idle()

    assert started, (
        f"port 65535 was refused, so the upper bound is off by one; "
        f"status {status!r}"
    )


def test_zero_latency_is_accepted(app, ports):
    """A latency of zero is valid and starts the relay.

    Zero is a legitimate setting — it turns SnailNet into a plain relay — and
    is easy to reject accidentally alongside the blank field.
    """
    app.configure("127.0.0.1", ports.listen, "127.0.0.1", ports.target, 0)
    app.start()

    started = app.wait_running(timeout=8)
    status = app.status_line()

    if started:
        app.stop()
        app.wait_idle()

    assert started, f"a latency of zero was refused; status {status!r}"
