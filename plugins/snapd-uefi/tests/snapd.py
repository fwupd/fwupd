#!/usr/bin/env python3
#
# Copyright 2024 Maciej Borzecki <maciej.borzecki@canonical.cm>
#
# SPDX-License-Identifier: LGPL-2.1-or-later

"""The app provides a mock snapd API for use with fwupd unit tests. The
/v2/system-secureboot endpoint mimics the behavior of snapd wrt. DBX updates.

The app exposes a couple of test specific endpoinds, /test/scenario, which picks
one of the scenarios (expected to be called in when setting a test fixture),
/test/reset, for resetting the state (usually called in tear down), and
/test/stats which provides the count of actions invoked since last reset.
"""

import argparse
import logging
import base64
import json
import os.path
from io import StringIO
from dataclasses import dataclass
from collections.abc import Callable
from typing import Any, Optional

from flask import Flask, Response, request, current_app


@dataclass
class Scenario:
    handler: Callable[[dict[str, Any]], Response]


@dataclass
class State:
    scenario: Optional[str]
    stats: dict[str, int]

    def __init__(self):
        self.scenario = None
        self.stats = {k: 0 for k in supported_actions}


app = Flask(__name__)

supported_actions = [
    "efi-secureboot-update-startup",
    "efi-secureboot-update-db-cleanup",
    "efi-secureboot-update-db-prepare",
]


def assert_no_extra_keys(d: dict[str, Any], allowed: list[str]):
    unexpected = [a for a in d.keys() if a not in allowed]
    assert len(unexpected) == 0, f"unexpected keys in request data: {unexpected}"


def assert_prepare_req(req: dict[str, Any]):
    assert req.get("action") == "efi-secureboot-update-db-prepare", "unexpected action"
    assert "payload" in req, "missing payload field"
    assert "key-database" in req, "missing key-database field"
    assert req.get("key-database") == "DBX", "unexpected key database"

    payload = req.get("payload")
    assert_no_extra_keys(req, ["action", "payload", "key-database"])
    assert payload is not None, "no update payload"
    # payload must be valid base64
    try:
        raw_payload = base64.urlsafe_b64decode(payload)
        with open(os.path.join(current_app.datadir, "dbx-update.auth"), "rb") as inf:
            reference_payload = inf.read()

        assert raw_payload == reference_payload, "unexpected payload content"
    except Exception as err:
        raise AssertionError("invalid base64 data in request") from err


def assert_startup_req(req: dict[str, Any]):
    assert req.get("action") == "efi-secureboot-update-startup", "unexpected action"
    assert_no_extra_keys(req, ["action"])


def assert_cleanup_req(req: dict[str, Any]):
    assert req.get("action") == "efi-secureboot-update-db-cleanup", "unexpected action"
    assert_no_extra_keys(req, ["action"])


def happy_startup(req: dict[str, Any]) -> Response:
    assert_startup_req(req)
    return Response(None, status=200)


def not_supported(req: dict[str, Any]) -> Response:
    assert_startup_req(req)
    # pretend relevant APIs are missing, hence 404
    return Response("", status=404)


def failed_startup(req: dict[str, Any]) -> Response:
    assert_startup_req(req)
    return Response(None, status=400)


def happy_prepare(req: dict[str, Any]) -> Response:
    assert_prepare_req(req)
    return Response("", status=200)


def failed_prepare(req: dict[str, Any]) -> Response:
    action = req.get("action")
    if action == "efi-secureboot-update-db-cleanup":
        return happy_cleanup(req)
    if action == "efi-secureboot-update-db-prepare":
        assert_prepare_req(req)
        return Response(
            json.dumps(
                {
                    "status": "500",
                    "error": {
                        "kind": "internal-error",
                        "message": "cannot reseal keys in prepare",
                    },
                }
            ),
            status=500,
        )
    if action == "efi-secureboot-update-startup":
        return happy_startup(req)

    raise AssertionError(f"unexpected action {action}")


def failed_cleanup(req: dict[str, Any]) -> Response:
    action = req.get("action")
    if action == "efi-secureboot-update-db-cleanup":
        assert_cleanup_req(req)
        return Response(
            json.dumps(
                {
                    "status": "500",
                    "error": {
                        "kind": "internal-error",
                        "message": "cannot reseal keys in cleanup",
                    },
                }
            ),
            status=500,
        )
    if action == "efi-secureboot-update-db-prepare":
        return happy_prepare(req)
    if action == "efi-secureboot-update-startup":
        return happy_startup(req)

    raise AssertionError(f"unexpected action {action}")


def happy_cleanup(req: dict[str, Any]) -> Response:
    assert_cleanup_req(req)
    return Response("", status=200)


def happy_update(req: dict[str, Any]) -> Response:
    action = req.get("action")
    if action == "efi-secureboot-update-db-cleanup":
        return happy_cleanup(req)
    if action == "efi-secureboot-update-db-prepare":
        return happy_prepare(req)
    if action == "efi-secureboot-update-startup":
        return happy_startup(req)

    raise AssertionError(f"unexpected action {action}")


playbook: dict[str, Scenario] = {
    # successful startup
    "happy-startup": Scenario(handler=happy_startup),
    # startup with mock failure
    "failed-startup": Scenario(handler=failed_startup),
    # 404 on the API endpoint, indicating lack of support on the snapd side
    "not-supported": Scenario(handler=not_supported),
    # prepare step fails
    "failed-prepare": Scenario(handler=failed_prepare),
    # prepare is successful, but cleanup fails
    "failed-cleanup": Scenario(handler=failed_cleanup),
    # successful update cycle
    "happy-update": Scenario(handler=happy_update),
}


def assert_scenario(f):
    def do():
        with app.app_context():
            assert current_app.state.scenario is not None, "test scenario is not set"
            return f()

    return do


def app_init_state():
    with app.app_context():
        current_app.state = State()


@app.route("/v2/system-secureboot", methods=["POST"])
@assert_scenario
def system_secureboot():
    assert len(request.view_args) == 0
    assert request.headers.get("Content-Type") == "application/json"

    req = request.get_json()
    logging.debug("req: %r", req)
    action = req.get("action")

    assert action in supported_actions, f"unknown action {action}"

    current_app.state.stats[action] += 1

    return playbook[current_app.state.scenario].handler(req)


@app.route("/test/setup", methods=["POST"])
def test_setup():
    req = request.get_json()
    logging.debug("req: %r", req)
    scenario = req.get("scenario")

    assert scenario in playbook, f"unknown scenario {scenario}"

    logging.debug("setting scenario to '%s'", scenario)
    current_app.state.scenario = scenario
    return Response("", status=200)


@app.route("/test/reset", methods=["POST"])
def test_reset():
    app_init_state()
    return Response("", status=200)


@app.route("/test/stats")
def test_stats():
    out = StringIO()
    # use a key-file format so that glib side parsing is easy
    out.write("[stats]\n")
    for action in sorted(current_app.state.stats.keys()):
        out.write(f"{action}={current_app.state.stats[action]}\n")
    return Response(out.getvalue(), status=200)


def parse_arguments():
    parser = argparse.ArgumentParser(description="mock snapd APIs")
    parser.add_argument(
        "--socket", help="socket path", default="/tmp/mock-snapd-test.sock"
    )
    parser.add_argument(
        "--datadir",
        default=os.path.dirname(__file__),
        help="path to directory with test data files",
    )

    return parser.parse_args()


if __name__ == "__main__":
    opts = parse_arguments()
    logging.basicConfig(level=logging.DEBUG)
    logging.debug("socket path: %s", opts.socket)
    logging.debug("data dir: %s", opts.datadir)
    with app.app_context():
        current_app.datadir = opts.datadir
    app_init_state()
    app.run(host="unix://" + opts.socket)
