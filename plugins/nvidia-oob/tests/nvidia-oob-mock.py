#!/usr/bin/env python3
# pylint: disable=invalid-name,missing-docstring
#
# Copyright 2026 NVIDIA Corporation
# Author: Vishnu Raghav <vraghav@nvidia.com>
#
# SPDX-License-Identifier: LGPL-2.1-or-later
#
# A minimal BMC for the nvidia-oob self test. It prints "PORT <n>" once it is
# listening, so the test can start it on any free port.

import json
import sys
from email.parser import BytesParser
from email.policy import HTTP
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

SESSION_KEY = "nvidia-oob-session-key"
TASK = "/redfish/v1/TaskService/Tasks/1"
MONITOR = TASK + "/Monitor"
GPU_CHASSIS = "/redfish/v1/Chassis/GPU_0"
GPU_PCIE_DEVICE = GPU_CHASSIS + "/PCIeDevices/PCIe_GPU_0"


class State:
    uploads = 0
    task_polls = 0
    gpu_staged = False
    aux_power_cycles = 0


def _inventory_member(member_id, version, related=None, staged=False):
    res = {
        "@odata.id": "/redfish/v1/UpdateService/FirmwareInventory/" + member_id,
        "Id": member_id,
        # the BMC names every entry this
        "Name": "Software Inventory",
        "Manufacturer": "NVIDIA",
        "Version": version,
        "Updateable": True,
    }
    if related is not None:
        res["RelatedItem"] = [{"@odata.id": related}]
    if staged:
        res["Oem"] = {
            "Nvidia": {
                "ActiveFirmwareSlot": {
                    "FirmwareState": "PendingActivation",
                    "Version": "GB300-2.00",
                }
            }
        }
    return res


def _get(path):
    if path == "/redfish/v1/UpdateService":
        return {
            "ServiceEnabled": True,
            "MultipartHttpPushUri": "/FWUpdate",
            "MaxImageSizeBytes": 268435456,
            "FirmwareInventory": {
                "@odata.id": "/redfish/v1/UpdateService/FirmwareInventory"
            },
        }
    if path == "/redfish/v1/UpdateService/FirmwareInventory":
        base = "/redfish/v1/UpdateService/FirmwareInventory/"
        return {
            "Members": [
                {"@odata.id": base + "FW_BMC_0"},
                {"@odata.id": base + "FW_GPU_0"},
            ]
        }
    if path == "/redfish/v1/UpdateService/FirmwareInventory/FW_BMC_0":
        # the BMC reports no slot state for its own image
        return _inventory_member("FW_BMC_0", "GB300-1.00")
    if path == "/redfish/v1/UpdateService/FirmwareInventory/FW_GPU_0":
        return _inventory_member(
            "FW_GPU_0", "97.00.01", related=GPU_CHASSIS, staged=State.gpu_staged
        )
    if path == GPU_CHASSIS:
        return {"PCIeDevices": {"@odata.id": GPU_CHASSIS + "/PCIeDevices"}}
    if path == GPU_CHASSIS + "/PCIeDevices":
        return {"Members": [{"@odata.id": GPU_PCIE_DEVICE}]}
    if path == GPU_PCIE_DEVICE:
        return {"PCIeFunctions": {"@odata.id": GPU_PCIE_DEVICE + "/PCIeFunctions"}}
    if path == GPU_PCIE_DEVICE + "/PCIeFunctions":
        return {"Members": [{"@odata.id": GPU_PCIE_DEVICE + "/PCIeFunctions/0"}]}
    if path == GPU_PCIE_DEVICE + "/PCIeFunctions/0":
        return {
            "VendorId": "0x10DE",
            "DeviceId": "0x31C2",
            "SubsystemVendorId": "0x10DE",
            "SubsystemId": "0x20E5",
        }
    if path == "/redfish/v1/Chassis/BMC_0":
        return {
            "Actions": {
                "Oem": {
                    "#NvidiaChassis.AuxPowerReset": {
                        "target": "/redfish/v1/Chassis/BMC_0/Actions/Oem/"
                        "NvidiaChassis.AuxPowerReset"
                    }
                }
            }
        }
    return None


class Handler(BaseHTTPRequestHandler):
    def log_message(self, fmt, *args):  # pylint: disable=arguments-differ
        sys.stderr.write("mock: " + fmt % args + "\n")

    def _reply(self, status, body=None, headers=None):
        data = b"" if body is None else json.dumps(body).encode()
        self.send_response(status)
        for key, value in (headers or {}).items():
            self.send_header(key, value)
        if data:
            self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(data)))
        self.end_headers()
        self.wfile.write(data)

    def _authorized(self):
        if self.headers.get("X-Auth-Token") == SESSION_KEY:
            return True
        self._reply(401)
        return False

    def do_GET(self):  # pylint: disable=invalid-name
        if not self._authorized():
            return
        if self.path == TASK:
            # running on the first poll, then reaped
            State.task_polls += 1
            if State.task_polls == 1:
                return self._reply(
                    200,
                    {"TaskState": "Running", "TaskStatus": "OK", "PercentComplete": 50},
                )
            return self._reply(404)
        if self.path == MONITOR:
            # once the task is done the monitor answers 200 with an empty body
            return self._reply(200)
        res = _get(self.path)
        if res is None:
            return self._reply(404)
        return self._reply(200, res)

    def do_POST(self):  # pylint: disable=invalid-name
        if not self._authorized():
            return
        body = self.rfile.read(int(self.headers.get("Content-Length", "0")))
        if self.path == "/FWUpdate":
            return self._upload(body)
        if self.path.endswith("NvidiaChassis.AuxPowerReset"):
            if json.loads(body).get("ResetType") != "AuxPowerCycleForce":
                return self._reply(400)
            State.aux_power_cycles += 1
            State.gpu_staged = False
            return self._reply(204)
        return self._reply(404)

    def _upload(self, body):
        header = (
            b"Content-Type: " + self.headers.get("Content-Type").encode() + b"\r\n\r\n"
        )
        msg = BytesParser(policy=HTTP).parsebytes(header + body)
        params = None
        for part in msg.iter_parts():
            if (
                part.get_param("name", header="content-disposition")
                == "UpdateParameters"
            ):
                params = json.loads(part.get_content())
        # the BMC rejects a populated Targets array, needs ForceUpdate, and only
        # accepts Immediate
        if (
            params is None
            or params.get("Targets") != []
            or params.get("ForceUpdate") is not True
            or params.get("@Redfish.OperationApplyTime") != "Immediate"
        ):
            return self._reply(400)
        State.uploads += 1
        State.task_polls = 0
        State.gpu_staged = True
        # the task monitor comes back three ways: a Location header, @odata.id in
        # the body, and a malformed @odata.id that has to be rejected
        if State.uploads == 1:
            return self._reply(202, {}, {"Location": MONITOR})
        if State.uploads == 2:
            return self._reply(202, {"@odata.id": MONITOR})
        return self._reply(202, {"@odata.id": 1})


def main():
    server = ThreadingHTTPServer(("127.0.0.1", 0), Handler)
    print(f"PORT {server.server_address[1]}", flush=True)
    server.serve_forever()


if __name__ == "__main__":
    main()
