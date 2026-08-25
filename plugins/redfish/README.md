---
title: Plugin: Redfish
---

## Introduction

Redfish is an open industry standard specification and schema that helps enable
simple and secure management of modern scalable platform hardware.

By specifying a RESTful interface and utilizing JSON and OData, Redfish helps
customers integrate solutions within their existing tool chains.

## Firmware Format

The daemon will decompress the cabinet archive and extract a firmware blob in
an unspecified binary file format.

This plugin supports the following protocol ID:

* `org.dmtf.redfish`

## GUID Generation

These devices use the provided GUID provided in the `SoftwareId` property without modification if
it is a valid GUID. On HPE machines the `Hpe/DeviceClass` and `Hpe/Targets[]` GUIDs are also added
if provided.

If the `SoftwareId` property is not a GUID then the vendor instance ID is used instead:

* `REDFISH\VENDOR_${RedfishManufacturer}&SOFTWAREID_${RedfishSoftwareId}`

Additionally, on Dell hardware the SystemID is also used:

* `REDFISH\VENDOR_${RedfishManufacturer}&SYSTEMID_${RedfishSystemID}&SOFTWAREID_${RedfishSoftwareId}`
* `REDFISH\VENDOR_${RedfishManufacturer}&SYSTEMID_${RedfishSystemID}&SOFTWAREID` (only-quirks)

Additionally, this Instance ID is added for quirk and parent matching:

* `REDFISH\VENDOR_${RedfishManufacturer}&ID_${RedfishId}`

On the NVIDIA DGX Station GB300 two further Instance IDs are added, as the BMC
reports `Name=Software Inventory` for every FirmwareInventory entry:

* `NVIDIA_OOB\URI_${odata.id}` -- always added, and unique per inventory entry
* `NVIDIA_OOB\SWID_${RedfishSoftwareId}` -- added when a `SoftwareId` is present,
  so that a unified-image archive can target several components at once

## Update Behavior

The firmware will be deployed as appropriate. The Redfish API does not specify
when the firmware will actually be written to the SPI device.

## Vendor ID Security

No vendor ID is set as there is no vendor field in the schema.

## Quirk Use

This plugin uses the following plugin-specific quirks:

### RedfishResetPreDelay

Delay in ms to use before querying the manager after a cleanup reset, default 0ms.

Since: 1.8.0

### RedfishResetPostDelay

Delay in ms to use before querying /redfish/v1/UpdateService after a cleanup reset,
default 0ms.

Since: 1.8.0

### `Flags=wildcard-targets`

Do not specify the `odata.id` in the multipart update Targets array and allow the BMC to deploy the
firmware onto all compatible hardware.

To use this option the payload must contain metadata that restricts it to a specific SoftwareId.

### `Flags=no-manager-reset-request`

The BMC device will auto-reboot and so fwupd should not explicitly call
`/redfish/v1/Managers/1/Actions/Manager.Reset`.

Since: 1.9.11

### `Flags=is-backup`

The device is the other half of a dual image firmware.

### `Flags=unsigned-build`

Use unsigned development builds.

### `Flags=manager-reset`

Reset the manager (typically the BMC) after updating this device.

## Session Token Authentication

Instead of storing a password in `/etc/fwupd/redfish.conf` the plugin can reuse a
Redfish `X-Auth-Token` created out of band. Set `FWUPD_SESSION_TOKEN_FILE` to the
path of a file containing the token, for example with a systemd drop-in:

```ini
[Service]
Environment=FWUPD_SESSION_TOKEN_FILE=/run/example-bmc-auth/session
```

The file should live on tmpfs and be readable only by root so that no secret is
written to persistent storage. When the variable is unset, or the file does not
exist, the plugin falls back to the configured username and password.

The file is re-read before each request rather than cached at startup, as the
session it names expires independently of the daemon. Replacing the file is
therefore enough to re-authenticate, with no need to restart `fwupd`.

## NVIDIA DGX Station GB300

The BMC is detected by probing `/redfish/v1/Chassis/Chassis_0` for
`Manufacturer=NVIDIA` and a `Model` containing both `GB300` and `Station`. A
matching BMC gets GB300-specific update semantics, which diverge from DMTF
DSP0266 in several places:

* `Targets` must be an empty array, as the BMC resolves the components to update
  from the PLDM bundle manifest; naming a component returns HTTP 400.
* `ForceUpdate` must be `true`, as the BMC otherwise rejects same-version installs.
* `@Redfish.OperationApplyTime` must be `Immediate`, as `OnReset` is not in the
  BMC's list of acceptable values.
* `PercentComplete` is only updated on `/redfish/v1/TaskService/Tasks/<id>`, so
  polling `/Tasks/<id>/Monitor` always reports 0%.
* Once a task completes the BMC returns HTTP 200 with an empty body for the task
  monitor rather than the HTTP 404 that normally signals it has been reaped.

This device requires a session token, so see **Session Token Authentication**
above before installing firmware:

```shell
fwupdmgr get-devices        # lists FW_BMC_0, FW_GPU_0, FW_CPU_0, ...
fwupdmgr install firmware.cab --allow-reinstall
```

A full PLDM bundle flash takes about 20 minutes. The firmware is staged rather
than applied, and the device is marked as needing activation: it becomes active
only after an aux-rail power cycle.

```shell
fwupdmgr activate
```

This POSTs `{"ResetType":"AuxPowerCycleForce"}` to the OEM
`NvidiaChassis.AuxPowerReset` action advertised by `/redfish/v1/Chassis/BMC_0`,
which is not the chassis used to detect the GB300. The `Force` variant does not
wait for the host to shut down, so the system loses power as soon as the BMC
accepts the request -- save your work before activating.

## Setting Service IP Manually

The service IP may not be automatically discoverable due to the absence of
Type 42 entry in SMBIOS. In this case, you have to specify the service IP
to RedfishUri in /etc/fwupd/redfish.conf

Take HPE Gen10 for example, the service IP can be found with the following
command:

```shell
ilorest --nologo list --selector=EthernetInterface. -j
```

This command lists all network interfaces, and the Redfish service IP belongs
to one of "Manager Network" Interfaces. For example:

```json
    {
      "@odata.context": "/redfish/v1/$metadata#EthernetInterface.EthernetInterface",
      "@odata.id": "/redfish/v1/Managers/1/EthernetInterfaces/1/",
      "@odata.type": "#EthernetInterface.v1_0_3.EthernetInterface",
      "Description": "Configuration of this Manager Network Interface",
      "HostName": "myredfish",
      "IPv4Addresses": [
        {
          "SubnetMask": "255.255.255.0",
          "AddressOrigin": "DHCP",
          "Gateway": "192.168.0.1",
          "Address": "192.168.0.133"
        }
      ],
      ...
```

In this example, the service IP is "192.168.0.133".

Since the conventional HTTP port is 80 and HTTPS port is 443, we can set
RedfishUri to either "<http://192.168.0.133:80>" or "<https://192.168.0.133:443>"
and verify the uri with

```shell
curl http://192.168.0.133:80/redfish/v1/
```

or

```shell
curl -k https://192.168.0.133:443/redfish/v1/
```

## External Interface Access

This requires HTTP access to a given URL.

## Version Considerations

This plugin has been available since fwupd version `1.1.0`.
