#!/usr/bin/env python3
# pylint: disable=invalid-name,missing-docstring
#
# Copyright 2021 Richard Hughes <richard@hughsie.com>
#
# SPDX-License-Identifier: LGPL-2.1-or-later

import json

from flask import Flask, Response, request

app = Flask(__name__)

HARDCODED_USERNAME = "username2"
HARDCODED_SMC_USERNAME = "smc_username"
HARDCODED_UNL_USERNAME = "unlicensed_username"
HARDCODED_HPE_USERNAME = "hpe_username"
HARDCODED_DELL_USERNAME = "dell_username"
HARDCODED_NVIDIA_USERNAME = "nvidia_username"
HARDCODED_USERNAMES = {
    HARDCODED_USERNAME,
    HARDCODED_SMC_USERNAME,
    HARDCODED_UNL_USERNAME,
    HARDCODED_HPE_USERNAME,
    HARDCODED_DELL_USERNAME,
    HARDCODED_NVIDIA_USERNAME,
}
HARDCODED_PASSWORD = "password2"
HARDCODED_BEARER_TOKEN = "token2"
HARDCODED_SESSION_KEY = "1234eabcdeabcdeabcdeabcdeabc1234"

app._percentage545: int = 0
app._percentage546: int = 0
app._percentage547: int = 0
app._percentage548: int = 0
app._reassign_bmc: bool = False
app._bmc_reassigned: bool = False
app._empty_bmc: bool = False
app._hpeupdatestate: str = "Idle"
app._hpeupdateresult = None
app._nvidia_polls: int = 0


def _failure(msg: str, status=400):
    res = {
        "error": {"message": msg},
    }
    return Response(
        response=json.dumps(res), status=status, mimetype="application/json"
    )


def _username():
    # a session created out of band acts as the default user
    if (
        request.authorization is None
        and request.headers.get("X-Auth-Token") == HARDCODED_SESSION_KEY
    ):
        return HARDCODED_USERNAME
    return request.authorization["username"]


@app.route("/redfish/v1/")
def index():
    # reset counter
    app._percentage545 = 0
    app._percentage546 = 0
    app._percentage547 = 0
    app._percentage548 = 0
    app._reassign_bmc = False
    app._bmc_reassigned = False
    app._empty_bmc = False
    app._hpeupdatestate = "Idle"
    app._hpeupdateresult = None

    # a session created out of band is used instead of a username and password
    if request.authorization is None and "X-Auth-Token" in request.headers:
        if request.headers["X-Auth-Token"] != HARDCODED_SESSION_KEY:
            return _failure("unauthorised", status=401)
        username = HARDCODED_USERNAME

    # a bearer token is used instead of a username and password, e.g. when talking
    # to a proxy that injects the real BMC credentials
    elif request.authorization is not None and request.authorization.type == "bearer":
        if request.authorization.token != HARDCODED_BEARER_TOKEN:
            return _failure("unauthorised", status=401)
        username = HARDCODED_USERNAME
    else:
        # check password from the config file
        try:
            if (
                request.authorization["username"] not in HARDCODED_USERNAMES
                or request.authorization["password"] != HARDCODED_PASSWORD
            ):
                return _failure("unauthorised", status=401)
        except (KeyError, TypeError):
            return _failure("invalid")
        username = request.authorization["username"]

    res = {
        "@odata.id": "/redfish/v1/",
        "@odata.etag": "653b835e9ee4af9ea7ea",
        "RedfishVersion": "1.6.0",
        "UUID": "92384634-2938-2342-8820-489239905423",
        "UpdateService": {"@odata.id": "/redfish/v1/UpdateService"},
    }

    if username == HARDCODED_HPE_USERNAME:
        res["Vendor"] = "HPE"

    if username == HARDCODED_DELL_USERNAME:
        res["Vendor"] = "Dell"

    if username == HARDCODED_NVIDIA_USERNAME:
        res["Vendor"] = "NVIDIA"

    if username in (
        HARDCODED_SMC_USERNAME,
        HARDCODED_UNL_USERNAME,
    ):
        res["Vendor"] = "SMCI"

    return Response(json.dumps(res), status=200, mimetype="application/json")


@app.route("/redfish/v1/Systems")
def systems():
    res = {
        "@odata.id": "/redfish/v1/Systems",
        "@odata.type": "#ComputerSystemCollection.ComputerSystemCollection",
        "Members": [
            {"@odata.id": "/redfish/v1/Systems/System.Embedded.1"},
        ],
        "Members@odata.count": 1,
    }
    return Response(json.dumps(res), status=200, mimetype="application/json")


@app.route("/redfish/v1/Systems/System.Embedded.1")
def system():
    res = {
        "@odata.id": "/redfish/v1/Systems/System.Embedded.1",
        "@odata.type": "#ComputerSystem.v1_20_1.ComputerSystem",
    }

    if _username() == HARDCODED_DELL_USERNAME:
        res["Oem"] = {
            "Dell": {
                "DellSystem": {
                    "SystemID": 3168,
                }
            }
        }
    return Response(json.dumps(res), status=200, mimetype="application/json")


@app.route("/redfish/v1/UpdateService")
def update_service():
    res = {
        "@odata.id": "/redfish/v1/UpdateService",
        "@odata.type": "#UpdateService.v1_8_0.UpdateService",
        "@odata.etag": "653b835e9ee4af9ea7ea",
        "FirmwareInventory": {
            "@odata.id": "/redfish/v1/UpdateService/FirmwareInventory"
        },
        "HttpPushUri": "/FWUpdate",
        "HttpPushUriOptions": {
            "HttpPushUriApplyTime": {
                "ApplyTime": "Immediate",
            }
        },
        "HttpPushUriOptionsBusy": False,
        "ServiceEnabled": True,
    }

    if _username() == HARDCODED_UNL_USERNAME:
        res["MultipartHttpPushUri"] = "/FWUpdate-unlicensed"
        res["Actions"] = {
            "#UpdateService.StartUpdate": {
                "target": "/redfish/v1/UpdateService/Actions/UpdateService.StartUpdate"
            }
        }
    elif _username() == HARDCODED_SMC_USERNAME:
        res["MultipartHttpPushUri"] = "/FWUpdate-smc"
        res["Actions"] = {
            "#UpdateService.StartUpdate": {
                "target": "/redfish/v1/UpdateService/Actions/UpdateService.StartUpdate"
            }
        }
    elif _username() == HARDCODED_HPE_USERNAME:
        res["Oem"] = {"Hpe": {"State": app._hpeupdatestate}}
        if app._hpeupdateresult is not None:
            res["Oem"]["Hpe"]["Result"] = app._hpeupdateresult
        res["HttpPushUri"] = "/FWUpdate-hpe"
    elif _username() == HARDCODED_NVIDIA_USERNAME:
        res["MultipartHttpPushUri"] = "/FWUpdate-nvidia"
    else:
        res["MultipartHttpPushUri"] = "/FWUpdate"

    return Response(json.dumps(res), status=200, mimetype="application/json")


DELL_NVME_ID = "Installed-0-1.0.1__Disk.Bay.0:Enclosure.Internal.0-4"


@app.route("/redfish/v1/UpdateService/FirmwareInventory")
def firmware_inventory():
    res = {
        "@odata.id": "/redfish/v1/UpdateService/FirmwareInventory",
        "@odata.type": "#SoftwareInventoryCollection.SoftwareInventoryCollection",
        "@odata.etag": "653b835e9ee4af9ea7ea",
        "Members": [
            {"@odata.id": "/redfish/v1/UpdateService/FirmwareInventory/BMC"},
            {"@odata.id": "/redfish/v1/UpdateService/FirmwareInventory/BIOS"},
        ],
        "Members@odata.count": 2,
    }
    if _username() == HARDCODED_NVIDIA_USERNAME:
        res["Members"] = [
            {"@odata.id": "/redfish/v1/UpdateService/FirmwareInventory/FW_BMC_0"},
            {"@odata.id": "/redfish/v1/UpdateService/FirmwareInventory/FW_CPU_0"},
            {"@odata.id": "/redfish/v1/UpdateService/FirmwareInventory/FW_GPU_0"},
        ]
    if _username() == HARDCODED_DELL_USERNAME:
        res["Members"].append(
            {"@odata.id": f"/redfish/v1/UpdateService/FirmwareInventory/{DELL_NVME_ID}"}
        )
        res["Members@odata.count"] = 3
    return Response(json.dumps(res), status=200, mimetype="application/json")


@app.route(f"/redfish/v1/UpdateService/FirmwareInventory/{DELL_NVME_ID}")
def firmware_inventory_dell_nvme():
    """a Dell disk: SoftwareId and ComponentID are "0", only the PCI identity tells it apart"""
    return Response(
        json.dumps(
            {
                "@odata.id": f"/redfish/v1/UpdateService/FirmwareInventory/{DELL_NVME_ID}",
                "@odata.type": "#SoftwareInventory.v1_10_2.SoftwareInventory",
                "Id": DELL_NVME_ID,
                "Name": "PCIe SSD in Slot 0 in Bay 4",
                "Oem": {
                    "Dell": {
                        "DellSoftwareInventory": {
                            "ComponentID": "0",
                            "ComponentType": "FRMW",
                            "DeviceID": "51C3",
                            "Id": "DCIM:INSTALLED_0x23_304_C_Disk.Bay.0:Enclosure.Internal.0-4",
                            "IdentityInfoValue": ["DCIM:firmware:1344:51C3:1028:2278"],
                            "Status": "Installed",
                            "SubDeviceID": "2278",
                            "SubVendorID": "1028",
                            "VendorID": "1344",
                        }
                    }
                },
                "SoftwareId": "0",
                "Updateable": True,
                "Version": "1.0.1",
            }
        ),
        status=200,
        mimetype="application/json",
    )


@app.route("/redfish/v1/UpdateService/FirmwareInventory/FW_BMC_0")
def firmware_inventory_fw_bmc_0():
    res = {
        "@odata.id": "/redfish/v1/UpdateService/FirmwareInventory/FW_BMC_0",
        "@odata.type": "#SoftwareInventory.v1_4_0.SoftwareInventory",
        "Id": "FW_BMC_0",
        "Manufacturer": "NVIDIA",
        "Name": "Software Inventory",
        "Updateable": True,
        "Version": "GB300-1.00",
    }
    return Response(json.dumps(res), status=200, mimetype="application/json")


@app.route("/redfish/v1/UpdateService/FirmwareInventory/FW_CPU_0")
def firmware_inventory_fw_cpu_0():
    res = {
        "@odata.id": "/redfish/v1/UpdateService/FirmwareInventory/FW_CPU_0",
        "@odata.type": "#SoftwareInventory.v1_4_0.SoftwareInventory",
        "Id": "FW_CPU_0",
        "Manufacturer": "NVIDIA",
        "Name": "Software Inventory",
        "SoftwareId": "0x0038",
        "Updateable": True,
        "Version": "02.00.00",
    }
    return Response(json.dumps(res), status=200, mimetype="application/json")


@app.route("/redfish/v1/UpdateService/FirmwareInventory/FW_GPU_0")
def firmware_inventory_fw_gpu_0():
    res = {
        "@odata.id": "/redfish/v1/UpdateService/FirmwareInventory/FW_GPU_0",
        "@odata.type": "#SoftwareInventory.v1_4_0.SoftwareInventory",
        "Id": "FW_GPU_0",
        "Manufacturer": "NVIDIA",
        "Name": "Software Inventory",
        "RelatedItem": [{"@odata.id": "/redfish/v1/Chassis/GPU_0"}],
        "Updateable": True,
        "Version": "03.00.00",
    }
    return Response(json.dumps(res), status=200, mimetype="application/json")


@app.route("/redfish/v1/Chassis/GPU_0")
def chassis_gpu_0():
    res = {
        "@odata.id": "/redfish/v1/Chassis/GPU_0",
        "@odata.type": "#Chassis.v1_25_0.Chassis",
        "Id": "GPU_0",
        "PCIeDevices": {"@odata.id": "/redfish/v1/Chassis/GPU_0/PCIeDevices"},
    }
    return Response(json.dumps(res), status=200, mimetype="application/json")


@app.route("/redfish/v1/Chassis/GPU_0/PCIeDevices")
def chassis_gpu_0_pcie_devices():
    res = {
        "@odata.id": "/redfish/v1/Chassis/GPU_0/PCIeDevices",
        "Members": [{"@odata.id": "/redfish/v1/Chassis/GPU_0/PCIeDevices/GPU_0"}],
        "Members@odata.count": 1,
    }
    return Response(json.dumps(res), status=200, mimetype="application/json")


@app.route("/redfish/v1/Chassis/GPU_0/PCIeDevices/GPU_0")
def chassis_gpu_0_pcie_device():
    res = {
        "@odata.id": "/redfish/v1/Chassis/GPU_0/PCIeDevices/GPU_0",
        "@odata.type": "#PCIeDevice.v1_14_0.PCIeDevice",
        "Id": "GPU_0",
        "PCIeFunctions": {
            "@odata.id": "/redfish/v1/Chassis/GPU_0/PCIeDevices/GPU_0/PCIeFunctions"
        },
    }
    return Response(json.dumps(res), status=200, mimetype="application/json")


@app.route("/redfish/v1/Chassis/GPU_0/PCIeDevices/GPU_0/PCIeFunctions")
def chassis_gpu_0_pcie_functions():
    res = {
        "Members": [
            {
                "@odata.id": "/redfish/v1/Chassis/GPU_0/PCIeDevices/GPU_0/PCIeFunctions/0"
            },
            {
                "@odata.id": "/redfish/v1/Chassis/GPU_0/PCIeDevices/GPU_0/PCIeFunctions/1"
            },
        ],
    }
    return Response(json.dumps(res), status=200, mimetype="application/json")


@app.route("/redfish/v1/Chassis/GPU_0/PCIeDevices/GPU_0/PCIeFunctions/0")
def chassis_gpu_0_pcie_function_0():
    res = {
        "@odata.id": "/redfish/v1/Chassis/GPU_0/PCIeDevices/GPU_0/PCIeFunctions/0",
        "DeviceClass": "ProcessingAccelerators",
        "DeviceId": "0x1234",
        "SubsystemId": "0x5678",
        "SubsystemVendorId": "0x10DE",
        "VendorId": "0x10DE",
    }
    return Response(json.dumps(res), status=200, mimetype="application/json")


@app.route("/redfish/v1/Chassis/GPU_0/PCIeDevices/GPU_0/PCIeFunctions/1")
def chassis_gpu_0_pcie_function_1():
    res = {
        "@odata.id": "/redfish/v1/Chassis/GPU_0/PCIeDevices/GPU_0/PCIeFunctions/1",
        "DeviceId": "0x2345",
        "VendorId": "0x10DE",
    }
    return Response(json.dumps(res), status=200, mimetype="application/json")


@app.route("/redfish/v1/Managers")
def redfish_managers():
    res = {
        "@odata.id": "/redfish/v1/Managers",
        "@odata.type": "#ManagerCollection.ManagerCollection",
        "Members": [{"@odata.id": "/redfish/v1/Managers/BMC_0"}],
        "Members@odata.count": 1,
    }
    return Response(json.dumps(res), status=200, mimetype="application/json")


@app.route("/redfish/v1/Managers/BMC_0")
def redfish_managers_bmc_0():
    res = {
        "@odata.id": "/redfish/v1/Managers/BMC_0",
        "@odata.type": "#Manager.v1_14_0.Manager",
        "Id": "BMC_0",
        "ManagerType": "BMC",
        "Links": {
            "ActiveSoftwareImage": {
                "@odata.id": "/redfish/v1/UpdateService/FirmwareInventory/FW_BMC_0"
            },
        },
    }
    return Response(json.dumps(res), status=200, mimetype="application/json")


@app.route("/redfish/v1/UpdateService/FirmwareInventory/BMC")
def firmware_inventory_bmc():
    # emulate a completed task whose member readback returns an empty body
    if app._bmc_reassigned and app._empty_bmc:
        return Response(response="", status=200, mimetype="application/json")
    software_id = "UEFI-NIC-9" if app._bmc_reassigned else "UEFI-AFE1-6"
    res = {
        "@odata.id": "/redfish/v1/UpdateService/FirmwareInventory/BMC",
        "@odata.type": "#SoftwareInventory.v1_2_3.SoftwareInventory",
        "@odata.etag": "653b835e9ee4af9ea7ea",
        "Id": "BMC",
        "LowestSupportedVersion": "11A-0.12",
        "Name": "Lenovo BMC Firmware",
        "Oem": {
            "Hpe": {
                "DeviceClass": "aa148d2e-6e09-453e-bc6f-63baa5f5ccc4",
                "Targets": [
                    "00000000-0000-0000-0000-000000000229",
                    "00000000-0000-0000-0000-000001413436",
                ],
            }
        },
        "RelatedItem": [{"@odata.id": "/redfish/v1/Managers/BMC"}],
        "SoftwareId": software_id,
        "UefiDevicePaths": ["BMC(0x1,0x0ABCDEF)"],
        "Updateable": True,
        "Version": "11A-1.02",
        "ReleaseDate": "2019-03-15T00:00:00",
    }
    if _username() in {
        HARDCODED_UNL_USERNAME,
        HARDCODED_SMC_USERNAME,
    }:
        res["Manufacturer"] = "SMCI"
    else:
        res["Manufacturer"] = "Lenovo"

    if _username() == HARDCODED_DELL_USERNAME:
        res["Oem"] = {"Dell": {"DellSoftwareInventory": {"Status": "Installed"}}}
    return Response(json.dumps(res), status=200, mimetype="application/json")


@app.route("/redfish/v1/Managers/BMC")
def redfish_managers_bmc():
    res = {}
    return Response(json.dumps(res), status=200, mimetype="application/json")


@app.route("/redfish/v1/Chassis/1/PCIeDevices/slot_3/PCIeFunctions/slot_2.00")
def firmware_chassis_pcie_function_slot2():
    res = {
        "VendorId": "0x14e4",
        "FunctionId": 1,
        "SubsystemId": "0x4042",
        "DeviceClass": "NetworkController",
        "SubsystemVendorId": "0x17aa",
        "DeviceId": "0x165f",
        "RevisionId": "0x00",
        "ClassCode": "0x020000",
    }
    return Response(json.dumps(res), status=200, mimetype="application/json")


@app.route("/redfish/v1/Chassis/1/PCIeDevices/slot_3/PCIeFunctions")
def firmware_chassis_pcie_functions():
    res = {
        "Members": [
            {
                "@odata.id": "/redfish/v1/Chassis/1/PCIeDevices/slot_3/PCIeFunctions/slot_2.00"
            }
        ],
    }
    return Response(json.dumps(res), status=200, mimetype="application/json")


@app.route("/redfish/v1/Systems/437XR1138R2")
def firmware_systems_slot7():
    res = {
        "SerialNumber": "12345",
        "@odata.id": "/redfish/v1/Chassis/1/PCIeDevices/slot_3",
        "@odata.etag": "653b835e9ee4af9ea7ea",
        "PCIeFunctions": {
            "@odata.id": "/redfish/v1/Chassis/1/PCIeDevices/slot_3/PCIeFunctions"
        },
        "DeviceType": "SingleFunction",
    }
    return Response(json.dumps(res), status=200, mimetype="application/json")


@app.route("/redfish/v1/UpdateService/FirmwareInventory/BIOS")
def firmware_inventory_bios():
    res = {
        "@odata.id": "/redfish/v1/UpdateService/FirmwareInventory/BIOS",
        "@odata.type": "#SoftwareInventory.v1_2_3.SoftwareInventory",
        "@odata.etag": "653b835e9ee4af9ea7ea",
        "Id": "BIOS",
        "LowestSupportedVersion": "P79 v1.10",
        "Name": "Contoso BIOS Firmware",
        "RelatedItem": [{"@odata.id": "/redfish/v1/Systems/437XR1138R2"}],
        "SoftwareId": "FEE82A67-6CE2-4625-9F44-237AD2402C28",
        "Updateable": True,
        "Version": "P79 v1.45",
        "ReleaseDate": "2019-03-15T00:00:00Z",
    }
    if _username() in {
        HARDCODED_UNL_USERNAME,
        HARDCODED_SMC_USERNAME,
    }:
        res["Manufacturer"] = "SMCI"
    else:
        res["Manufacturer"] = "Contoso"
    return Response(json.dumps(res), status=200, mimetype="application/json")


@app.route("/redfish/v1/SessionService/Sessions", methods=["POST"])
def session_service_sessions():
    username = request.authorization["username"]

    if username not in HARDCODED_USERNAMES:
        return _failure("unauthorised")
    if request.authorization["password"] != HARDCODED_PASSWORD:
        return _failure("invalid password")

    res = {
        "@odata.id": "/redfish/v1/SessionService/Sessions/1",
        "@odata.type": "#Session.v1_0_0.Session",
        "@odata.etag": "653b835e9ee4af9ea7ea",
        "Id": "1",
        "Name": "Session 1",
        "UserName": username,
    }
    return Response(
        json.dumps(res),
        status=200,
        mimetype="application/json",
        headers={"X-Auth-Token": HARDCODED_SESSION_KEY},
    )


@app.route("/redfish/v1/SessionService/Sessions/<session_id>", methods=["DELETE"])
def session_service_session_delete(session_id):
    if request.headers.get("X-Auth-Token") != HARDCODED_SESSION_KEY:
        return _failure("unauthorised", status=401)
    return Response(status=200)


@app.route("/redfish/v1/TaskService/999")
def task_manager():
    res = {
        "@odata.id": "/redfish/v1/TaskService/999",
        "@odata.type": "#Task.v1_4_3.Task",
        "@odata.etag": "653b835e9ee4af9ea7ea",
        "Id": "545",
        "Name": "Task 545",
    }
    return Response(json.dumps(res), status=200, mimetype="application/json")


@app.route("/redfish/v1/TaskService/Tasks/545")
def task_status_545():
    res = {
        "@odata.id": "/redfish/v1/TaskService/Tasks/545",
        "@odata.type": "#Task.v1_4_3.Task",
        "@odata.etag": "653b835e9ee4af9ea7ea",
        "Id": "545",
        "Name": "Task 545",
        "PercentComplete": app._percentage545,
    }
    if app._percentage545 == 0:
        res["TaskState"] = "Running"
    elif app._percentage545 in [25, 50, 75]:
        res["TaskState"] = "Running"
        res["TaskStatus"] = "OK"
        res["Messages"] = [
            {
                "Message": "Applying image",
                "MessageId": "Update.1.1.TransferringToComponent",
            }
        ]
    elif app._percentage545 == 100:
        res["TaskState"] = "Completed"
        res["TaskStatus"] = "OK"
        res["Messages"] = [
            {
                "Message": "Applying image",
                "MessageId": "Update.1.1.TransferringToComponent",
            },
            {
                "Message": "A reset is required",
                "MessageId": "Base.1.10.ResetRequired",
            },
            {
                "Message": "Task completed OK",
                "MessageId": "TaskEvent.1.0.TaskCompletedOK",
            },
        ]
    else:
        res["TaskState"] = "Exception"
        res["TaskStatus"] = "Warning"
        res["Messages"] = [
            {
                "Message": "Error verifying image",
                "MessageId": "Update.1.0.ApplyFailed",
                "Severity": "Warning",
            }
        ]
    app._percentage545 += 25
    return Response(response=json.dumps(res), status=200, mimetype="application/json")


@app.route("/redfish/v1/TaskService/Tasks/546")
def task_status_546():
    res = {
        "@odata.type": "#Task.v1_4_3.Task",
        "@odata.id": "/redfish/v1/TaskService/Tasks/546",
        "@odata.etag": "653b835e9ee4af9ea7ea",
        "Id": "546",
        "Name": "BIOS Verify",
        "TaskState": "Running",
        "StartTime": "2022-09-29T14:50:54+00:00",
        "PercentComplete": app._percentage546,
        "HidePayload": True,
        "TaskMonitor": "/redfish/v1/TaskMonitor/gd5n5ffS4gi9r6YKVZmgIIaj8ECLfnc",
        "TaskStatus": "OK",
        "Messages": [
            {
                "MessageId": "",
                "RelatedProperties": [""],
                "Message": "",
                "MessageArgs": [""],
                "Severity": "",
            }
        ],
        "Oem": {},
    }
    if app._percentage546 == 0 or app._percentage546 in [25, 50, 75]:
        res["TaskState"] = "Running"
    elif app._percentage546 == 100:
        res["TaskState"] = "Completed"
    app._percentage546 += 25
    return Response(response=json.dumps(res), status=200, mimetype="application/json")


@app.route("/redfish/v1/TaskService/Tasks/547")
def task_status_547():
    res = {
        "@odata.id": "/redfish/v1/TaskService/Tasks/547",
        "@odata.type": "#Task.v1_4_3.Task",
        "@odata.etag": "653b835e9ee4af9ea7ea",
        "Id": "547",
        "Name": "Task 547",
        "PercentComplete": app._percentage547,
    }
    if app._percentage547 == 0:
        res["TaskState"] = "Running"
    elif app._percentage547 in [25, 50, 75]:
        res["TaskState"] = "Running"
        res["TaskStatus"] = "OK"
        res["Messages"] = [
            {
                "Message": "Applying image",
                "MessageId": "Update.1.1.TransferringToComponent",
            }
        ]
    elif app._percentage547 == 100:
        res["TaskState"] = "Completed"
        res["TaskStatus"] = "OK"
        res["Messages"] = [
            {
                "Message": "Task completed OK",
                "MessageId": "TaskEvent.1.0.TaskCompletedOK",
            }
        ]
        if app._reassign_bmc:
            app._bmc_reassigned = True
    else:
        res["TaskState"] = "Exception"
    app._percentage547 += 25
    return Response(response=json.dumps(res), status=200, mimetype="application/json")


@app.route("/redfish/v1/TaskService/Tasks/548")
def task_status_548():
    # signals a required reset before reaching Completed, which short-circuits
    # polling; the reassignment would only surface at true completion
    res = {
        "@odata.id": "/redfish/v1/TaskService/Tasks/548",
        "@odata.type": "#Task.v1_4_3.Task",
        "@odata.etag": "653b835e9ee4af9ea7ea",
        "Id": "548",
        "Name": "Task 548",
        "PercentComplete": app._percentage548,
    }
    if app._percentage548 == 0:
        res["TaskState"] = "Running"
    elif app._percentage548 in [25, 50, 75]:
        res["TaskState"] = "Running"
        res["TaskStatus"] = "OK"
        res["Messages"] = [
            {
                "Message": "A reset is required",
                "MessageId": "Base.1.10.ResetRequired",
            }
        ]
    elif app._percentage548 == 100:
        res["TaskState"] = "Completed"
        res["TaskStatus"] = "OK"
        if app._reassign_bmc:
            app._bmc_reassigned = True
    else:
        res["TaskState"] = "Exception"
    app._percentage548 += 25
    return Response(response=json.dumps(res), status=200, mimetype="application/json")


@app.route("/redfish/v1/TaskService/Tasks/<int:task_id>")
def task_status_nvidia(task_id: int):
    app._nvidia_polls += 1
    res = {
        "@odata.id": f"/redfish/v1/TaskService/Tasks/{task_id}",
        "@odata.type": "#Task.v1_4_3.Task",
        "Id": str(task_id),
        "PercentComplete": 50,
        "TaskState": "Running",
        "TaskStatus": "OK",
    }
    if task_id == 603:
        # a component failed to update
        res["TaskState"] = "Exception"
        res["TaskStatus"] = "Critical"
    elif app._nvidia_polls > 1:
        # the task is removed once it is done
        if task_id in [602, 604]:
            return _failure("not found", status=404)
        res["PercentComplete"] = 100
        res["TaskState"] = "Completed"
    return Response(response=json.dumps(res), status=200, mimetype="application/json")


@app.route("/redfish/v1/TaskService/Tasks/<int:task_id>/Monitor")
def task_monitor_nvidia(task_id: int):
    # the task is gone, but the monitor still reports that it failed
    if task_id == 604:
        res = {
            "@odata.id": f"/redfish/v1/TaskService/Tasks/{task_id}",
            "@odata.type": "#Task.v1_4_3.Task",
            "Id": str(task_id),
            "TaskState": "Exception",
            "TaskStatus": "Critical",
        }
        return Response(json.dumps(res), status=200, mimetype="application/json")

    # once the task is done the monitor answers with an empty body
    return Response(status=200)


@app.route("/FWUpdate-unlicensed", methods=["GET"])
def fwupdate_unlicensed():
    res = {
        "error": {
            "code": "Base.v1_4_0.GeneralError",
            "Message": "A general error has occurred. See ExtendedInfo for more information.",
            "@Message.ExtendedInfo": [
                {
                    "MessageId": "SMC.1.0.OemLicenseNotPassed",
                    "Message": "Feature not available.",
                    "MessageArgs": ["BIOS"],
                }
            ],
        }
    }
    return Response(json.dumps(res), status=405, mimetype="application/json")


@app.route("/FWUpdate-smc", methods=["GET"])
def fwupdate_smc_query():
    res = {"Accepted": {"code": "Base.v1_4_0.Accepted"}}
    return Response(json.dumps(res), status=200, mimetype="application/json")


@app.route("/FWUpdate-smc", methods=["POST"])
def fwupdate_smc():
    data = json.loads(request.form["UpdateParameters"])
    if data["@Redfish.OperationApplyTime"] != "OnStartUpdateRequest":
        return _failure("apply invalid")
    if data["Targets"][0] != "/redfish/v1/Systems/1/Bios":
        return _failure("id invalid")
    fileitem = request.files["UpdateFile"]
    if not fileitem.filename.endswith(".bin"):
        return _failure("filename invalid")
    filecontents = fileitem.read().decode()
    if filecontents == "hello":
        app._percentage546 = 0
        res = {
            "Accepted": {
                "code": "Base.v1_4_0.Accepted",
                "Message": "Successfully Accepted Request. Please see the location header and ExtendedInfo for more information.",
                "@Message.ExtendedInfo": [
                    {
                        "MessageId": "SMC.1.0.OemSimpleupdateAcceptedMessage",
                        "Severity": "Ok",
                        "Resolution": "No resolution was required.",
                        "Message": "Please also check Task Resource /redfish/v1/TaskService/Tasks/546 to see more information.",
                        "MessageArgs": ["/redfish/v1/TaskService/Tasks/546"],
                        "RelatedProperties": ["BiosVerifyAccepted"],
                    }
                ],
            }
        }
        # Location set to the URI of a task monitor.
        return Response(
            json.dumps(res),
            status=202,
            mimetype="application/json",
            headers={"Location": "/redfish/v1/TaskService/Tasks/546"},
        )
    elif filecontents == "stuck":
        res = {
            "error": {
                "code": "Base.v1_4_0.GeneralError",
                "Message": "A general error has occurred. See ExtendedInfo for more information.",
                "@Message.ExtendedInfo": [
                    {
                        "MessageId": "SMC.1.0.OemFirmwareAlreadyInUpdateMode",
                        "Severity": "Warning",
                        "Resolution": "Please check if there was the next step with respective API to execute.",
                        "Message": "The BIOS firmware update was already in update mode.",
                        "MessageArgs": ["BIOS"],
                        "RelatedProperties": ["EnterUpdateMode_StatusCheck"],
                    }
                ],
            }
        }
        return Response(json.dumps(res), status=405, mimetype="application/json")
    else:
        return _failure("data invalid")


@app.route("/FWUpdate-hpe", methods=["POST"])
def fwupdate_hpe():
    print(request.form)
    if not request.form["sessionKey"]:
        return _failure("no sessionKey", status=401)

    data = json.loads(request.form["parameters"])

    if not data["UpdateTarget"]:
        return _failure("payload will not update the target")
    if data["UpdateRepository"]:
        return _failure("payload will update the repository")

    fileitem = request.files["files[]"]
    if not fileitem:
        return _failure("no file supplied")
    filecontents = fileitem.read().decode()

    if filecontents == "hello":
        app._hpeupdatestate = "Complete"
        app._hpeupdateresult = None
    elif filecontents == "reboot":
        app._hpeupdatestate = "Error"
        app._hpeupdateresult = {
            "Message": "A reset is required",
            "MessageId": "iLO.2.25.SystemResetRequired",
        }
    else:
        return _failure("data invalid")
    return Response(status=200)


@app.route("/FWUpdate", methods=["POST"])
def fwupdate():
    data = json.loads(request.form["UpdateParameters"])
    if data["@Redfish.OperationApplyTime"] != "Immediate":
        return _failure("apply invalid")
    if data["Targets"][0] != "/redfish/v1/UpdateService/FirmwareInventory/BMC":
        return _failure("id invalid")
    if len(request.files) != 1:
        return _failure("no file supplied")
    fileitem = request.files["UpdateFile"]
    if not fileitem.filename.endswith(".exe"):
        return _failure("filename invalid")
    contents = fileitem.read().decode()
    app._reassign_bmc = False
    app._bmc_reassigned = False
    app._empty_bmc = False
    if contents == "hello":
        location = "/redfish/v1/TaskService/Tasks/545"
    elif contents == "same":
        # completes cleanly without a reset, and the member keeps its SoftwareId
        app._percentage547 = 0
        location = "/redfish/v1/TaskService/Tasks/547"
    elif contents == "shift":
        # reassign the member once the task reaches completion
        app._reassign_bmc = True
        app._percentage547 = 0
        location = "/redfish/v1/TaskService/Tasks/547"
    elif contents == "empty-shift":
        # completes cleanly, but the member readback then has an empty body
        app._reassign_bmc = True
        app._empty_bmc = True
        app._percentage547 = 0
        location = "/redfish/v1/TaskService/Tasks/547"
    elif contents == "reset-shift":
        # same, but the task requires a reset before it completes
        app._reassign_bmc = True
        app._percentage548 = 0
        location = "/redfish/v1/TaskService/Tasks/548"
    else:
        return _failure("data invalid")
    res = {
        "Version": "P79 v1.45",
        "@odata.id": location,
        "@odata.etag": "653b835e9ee4af9ea7ea",
        "TaskMonitor": "/redfish/v1/TaskService/999",
    }
    # Location set to the URI of a task monitor.
    return Response(
        json.dumps(res),
        status=202,
        mimetype="application/json",
        headers={"Location": location},
    )


@app.route("/FWUpdate-nvidia", methods=["POST"])
def fwupdate_nvidia():
    data = json.loads(request.form["UpdateParameters"])
    if data["Targets"]:
        return _failure("targets invalid")
    if data["ForceUpdate"] is not True:
        return _failure("force invalid")
    if data["@Redfish.OperationApplyTime"] != "Immediate":
        return _failure("apply invalid")
    fileitem = request.files["UpdateFile"]
    if fileitem.filename != "firmware.bin":
        return _failure("filename invalid")
    contents = fileitem.read().decode()
    app._nvidia_polls = 0
    if contents == "completed":
        res = {"@odata.id": "/redfish/v1/TaskService/Tasks/601/Monitor"}
        return Response(json.dumps(res), status=202, mimetype="application/json")
    if contents == "no-task":
        res = {"@odata.id": 601}
        return Response(json.dumps(res), status=202, mimetype="application/json")
    if contents == "reaped":
        # only the Location header gives the task monitor
        res = {"TaskState": "Running"}
        location = "/redfish/v1/TaskService/Tasks/602/Monitor"
    elif contents == "failed":
        res = {"@odata.id": "/redfish/v1/TaskService/Tasks/603"}
        location = "/redfish/v1/TaskService/Tasks/603/Monitor"
    elif contents == "monitor-failed":
        res = {"@odata.id": "/redfish/v1/TaskService/Tasks/604"}
        location = "/redfish/v1/TaskService/Tasks/604/Monitor"
    else:
        return _failure("data invalid")
    return Response(
        json.dumps(res),
        status=202,
        mimetype="application/json",
        headers={"Location": location},
    )


@app.route(
    "/redfish/v1/UpdateService/Actions/UpdateService.StartUpdate", methods=["POST"]
)
def startupdate():
    res = {
        "Accepted": {
            "code": "Base.v1_4_0.Accepted",
            "Message": "Successfully Accepted Request. Please see the location header and ExtendedInfo for more information.",
            "@Message.ExtendedInfo": [
                {
                    "MessageId": "SMC.1.0.OemSimpleupdateAcceptedMessage",
                    "Severity": "Ok",
                    "Resolution": "No resolution was required.",
                    "Message": "Please also check Task Resource /redfish/v1/TaskService/Tasks/546 to see more information.",
                    "MessageArgs": ["/redfish/v1/TaskService/Tasks/546"],
                    "RelatedProperties": ["BiosUpdateAccepted"],
                }
            ],
        }
    }
    app._percentage546 = 0
    return Response(
        json.dumps(res),
        status=202,
        mimetype="application/json",
        headers={"Location": "/redfish/v1/TaskService/Tasks/546"},
    )


if __name__ == "__main__":
    app.run(host="0.0.0.0", port=4661)
