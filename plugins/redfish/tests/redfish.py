#!/usr/bin/env python3
# pylint: disable=invalid-name,missing-docstring
#
# Copyright 2021 Richard Hughes <richard@hughsie.com>
#
# SPDX-License-Identifier: LGPL-2.1-or-later

import json

from flask import Flask, Response, request

app = Flask(__name__)

HARDCODED_SMC_USERNAME = "smc_username"
HARDCODED_UNL_USERNAME = "unlicensed_username"
HARDCODED_HPE_USERNAME = "hpe_username"
HARDCODED_DELL_USERNAME = "dell_username"
HARDCODED_USERNAMES = {
    "username2",
    HARDCODED_SMC_USERNAME,
    HARDCODED_UNL_USERNAME,
    HARDCODED_HPE_USERNAME,
    HARDCODED_DELL_USERNAME,
}
HARDCODED_PASSWORD = "password2"

app._percentage545: int = 0
app._percentage546: int = 0
app._hpeupdatestate: str = "Idle"


def _failure(msg: str, status=400):
    res = {
        "error": {"message": msg},
    }
    return Response(
        response=json.dumps(res), status=status, mimetype="application/json"
    )


@app.route("/redfish/v1/")
def index():
    # reset counter
    app._percentage545 = 0
    app._percentage546 = 0
    app._hpeupdatestate = "Idle"

    # check password from the config file
    try:
        if (
            request.authorization["username"] not in HARDCODED_USERNAMES
            or request.authorization["password"] != HARDCODED_PASSWORD
        ):
            return _failure("unauthorised", status=401)
    except (KeyError, TypeError):
        return _failure("invalid")

    res = {
        "@odata.id": "/redfish/v1/",
        "@odata.etag": "653b835e9ee4af9ea7ea",
        "RedfishVersion": "1.6.0",
        "UUID": "92384634-2938-2342-8820-489239905423",
        "UpdateService": {"@odata.id": "/redfish/v1/UpdateService"},
    }

    if request.authorization["username"] == HARDCODED_HPE_USERNAME:
        res["Vendor"] = "HPE"

    if request.authorization["username"] == HARDCODED_DELL_USERNAME:
        res["Vendor"] = "Dell"

    if request.authorization["username"] in (
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

    if request.authorization["username"] == HARDCODED_DELL_USERNAME:
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

    if request.authorization["username"] == HARDCODED_UNL_USERNAME:
        res["MultipartHttpPushUri"] = "/FWUpdate-unlicensed"
        res["Actions"] = {
            "#UpdateService.StartUpdate": {
                "target": "/redfish/v1/UpdateService/Actions/UpdateService.StartUpdate"
            }
        }
    elif request.authorization["username"] == HARDCODED_SMC_USERNAME:
        res["MultipartHttpPushUri"] = "/FWUpdate-smc"
        res["Actions"] = {
            "#UpdateService.StartUpdate": {
                "target": "/redfish/v1/UpdateService/Actions/UpdateService.StartUpdate"
            }
        }
    elif request.authorization["username"] == HARDCODED_HPE_USERNAME:
        res["Oem"] = {"Hpe": {"State": app._hpeupdatestate}}
        res["HttpPushUri"] = "/FWUpdate-hpe"
    else:
        res["MultipartHttpPushUri"] = "/FWUpdate"

    return Response(json.dumps(res), status=200, mimetype="application/json")


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
    return Response(json.dumps(res), status=200, mimetype="application/json")


@app.route("/redfish/v1/UpdateService/FirmwareInventory/BMC")
def firmware_inventory_bmc():
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
        "SoftwareId": "UEFI-AFE1-6",
        "UefiDevicePaths": ["BMC(0x1,0x0ABCDEF)"],
        "Updateable": True,
        "Version": "11A-1.02",
        "ReleaseDate": "2019-03-15T00:00:00",
    }
    if request.authorization["username"] in {
        HARDCODED_UNL_USERNAME,
        HARDCODED_SMC_USERNAME,
    }:
        res["Manufacturer"] = "SMCI"
    else:
        res["Manufacturer"] = "Lenovo"

    if request.authorization["username"] == HARDCODED_DELL_USERNAME:
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
    if request.authorization["username"] in {
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
        headers={"X-Auth-Token": "1234eabcdeabcdeabcdeabcdeabc1234"},
    )


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
    if app._percentage546 == 0:
        res["TaskState"] = "Running"
    elif app._percentage546 in [25, 50, 75]:
        res["TaskState"] = "Running"
    elif app._percentage546 == 100:
        res["TaskState"] = "Completed"
    app._percentage546 += 25
    return Response(response=json.dumps(res), status=200, mimetype="application/json")


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

    app._hpeupdatestate = "Complete"
    return Response(status=200)


@app.route("/FWUpdate", methods=["POST"])
def fwupdate():
    data = json.loads(request.form["UpdateParameters"])
    if data["@Redfish.OperationApplyTime"] != "Immediate":
        return _failure("apply invalid")
    if data["Targets"][0] != "/redfish/v1/UpdateService/FirmwareInventory/BMC":
        return _failure("id invalid")
    fileitem = request.files["UpdateFile"]
    if not fileitem.filename.endswith(".bin"):
        return _failure("filename invalid")
    if fileitem.read().decode() != "hello":
        return _failure("data invalid")
    res = {
        "Version": "P79 v1.45",
        "@odata.id": "/redfish/v1/TaskService/Tasks/545",
        "@odata.etag": "653b835e9ee4af9ea7ea",
        "TaskMonitor": "/redfish/v1/TaskService/999",
    }
    # Location set to the URI of a task monitor.
    return Response(
        json.dumps(res),
        status=202,
        mimetype="application/json",
        headers={"Location": "/redfish/v1/TaskService/Tasks/545"},
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
