#!/usr/bin/python3
# pylint: disable=invalid-name,missing-docstring
#
# Copyright (C) 2021 Richard Hughes <richard@hughsie.com>
#
# SPDX-License-Identifier: LGPL-2.1+

import json

from flask import Flask, Response, request

app = Flask(__name__)

HARDCODED_USERNAME = "username2"
HARDCODED_PASSWORD = "password2"

app._percentage: int = 0


def _failure(msg: str, status=400):
    res = {
        "error": {"message": msg},
    }
    return Response(response=json.dumps(res), status=401, mimetype="application/json")


@app.route("/redfish/v1/")
def index():

    # reset counter
    app._percentage = 0

    # check password from the config file
    try:
        if (
            request.authorization["username"] != HARDCODED_USERNAME
            or request.authorization["password"] != HARDCODED_PASSWORD
        ):
            return _failure("unauthorised", status=401)
    except (KeyError, TypeError):
        return _failure("invalid")

    res = {
        "@odata.id": "/redfish/v1/",
        "RedfishVersion": "1.6.0",
        "UUID": "92384634-2938-2342-8820-489239905423",
        "UpdateService": {"@odata.id": "/redfish/v1/UpdateService"},
    }
    return Response(json.dumps(res), status=200, mimetype="application/json")


@app.route("/redfish/v1/UpdateService")
def update_service():

    res = {
        "@odata.id": "/redfish/v1/UpdateService",
        "@odata.type": "#UpdateService.v1_8_0.UpdateService",
        "FirmwareInventory": {
            "@odata.id": "/redfish/v1/UpdateService/FirmwareInventory"
        },
        "MultipartHttpPushUri": "/FWUpdate",
        "HttpPushUri": "/FWUpdate",
        "HttpPushUriOptions": {
            "HttpPushUriApplyTime": {
                "ApplyTime": "Immediate",
            }
        },
        "HttpPushUriOptionsBusy": False,
        "ServiceEnabled": True,
    }
    return Response(json.dumps(res), status=200, mimetype="application/json")


@app.route("/redfish/v1/UpdateService/FirmwareInventory")
def firmware_inventory():

    res = {
        "@odata.id": "/redfish/v1/UpdateService/FirmwareInventory",
        "@odata.type": "#SoftwareInventoryCollection.SoftwareInventoryCollection",
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
        "Id": "BMC",
        "LowestSupportedVersion": "11A-0.12",
        "Manufacturer": "Lenovo",
        "Name": "Lenovo BMC Firmware",
        "RelatedItem": [{"@odata.id": "/redfish/v1/Managers/BMC"}],
        "ReleaseDate": "2017-08-22T12:00:00",
        "SoftwareId": "UEFI-AFE1-6",
        "UefiDevicePaths": ["BMC(0x1,0x0ABCDEF)"],
        "Updateable": True,
        "Version": "11A-1.02",
        "ReleaseDate": "2019-03-15T00:00:00",
    }
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
        "Id": "BIOS",
        "LowestSupportedVersion": "P79 v1.10",
        "Manufacturer": "Contoso",
        "Name": "Contoso BIOS Firmware",
        "RelatedItem": [{"@odata.id": "/redfish/v1/Systems/437XR1138R2"}],
        "ReleaseDate": "2017-12-06T12:00:00",
        "SoftwareId": "FEE82A67-6CE2-4625-9F44-237AD2402C28",
        "Updateable": True,
        "Version": "P79 v1.45",
        "ReleaseDate": "2019-03-15T00:00:00Z",
    }
    return Response(json.dumps(res), status=200, mimetype="application/json")


@app.route("/redfish/v1/TaskService/999")
def task_manager():
    res = {
        "@odata.id": "/redfish/v1/TaskService/999",
        "@odata.type": "#Task.v1_4_3.Task",
        "Id": "545",
        "Name": "Task 545",
    }
    return Response(json.dumps(res), status=200, mimetype="application/json")


@app.route("/redfish/v1/TaskService/Tasks/545")
def task_status():

    res = {
        "@odata.id": "/redfish/v1/TaskService/Tasks/545",
        "@odata.type": "#Task.v1_4_3.Task",
        "Id": "545",
        "Name": "Task 545",
        "PercentComplete": app._percentage,
    }
    if app._percentage == 0:
        res["TaskState"] = "Running"
    elif app._percentage in [25, 50, 75]:
        res["TaskState"] = "Running"
        res["TaskStatus"] = "OK"
        res["Messages"] = [
            {
                "Message": "Applying image",
                "MessageId": "Update.1.1.TransferringToComponent",
            }
        ]
    elif app._percentage == 100:
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
    app._percentage += 25
    return Response(response=json.dumps(res), status=200, mimetype="application/json")


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
        "TaskMonitor": "/redfish/v1/TaskService/999",
    }
    # Location set to the URI of a task monitor.
    return Response(
        json.dumps(res),
        status=202,
        mimetype="application/json",
        headers={"Location": "http://localhost:4661/redfish/v1/TaskService/Tasks/545"},
    )


if __name__ == "__main__":
    app.run(host="0.0.0.0", port=4661)
