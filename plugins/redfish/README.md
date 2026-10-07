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

On the NVIDIA DGX Station GB300 these Instance IDs are added instead, as the BMC
reports `Name=Software Inventory` for every FirmwareInventory entry:

* `REDFISH\ID_${RedfishId}` -- the inventory `Id`, for example `FW_BMC_0`, which
  is stable and unique per entry
* `REDFISH\ID_${RedfishId}&VEN_${PciVendorId}&DEV_${PciDeviceId}&SUBSYS_${PciSubsystemVendorId}${PciSubsystemId}`
  -- for a component that comes in board variants taking different images, such
  as the GPU, from the PCIe function listed under the chassis its entry relates to

Neither includes the `Manufacturer`, which differs between the OEM-branded and the
NVIDIA-branded images on one station. These IDs are the same on every OEM's
system, so the devices set `enforce-requires`: a release must also match the
system's hardware ID (CHID). The `SoftwareId` is not used: this BMC reports the
PLDM component identifier there, which components running different firmware
share, such as the two SMAs.

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

### `Flags=no-redfish-vendor-id`

Do not add the `REDFISH:` vendor ID taken from the inventory `Manufacturer`, nor the `PCI:` one
taken from a related PCIe function, for a device whose vendor IDs come from the system instead.
The NVIDIA DGX Station GB300 devices set it.

### `Flags=related-chassis-pcie`

Follow a related chassis's `PCIeDevices` to their functions for the PCI IDs, as is done for a
related PCIe device. It is not the default, as a chassis can be the whole system, whose PCIe
devices belong to other components. The NVIDIA DGX Station GB300 devices set it.

### `Flags=nvidia-oob`

Set on the hardware ID (CHID) of a system whose BMC takes the NVIDIA DGX Station GB300 update
semantics described below; each `FirmwareInventory` member of that system is then its own device.

## Session Token Authentication

Instead of storing a password, the plugin can reuse a Redfish `X-Auth-Token`
created out of band by setting `SessionKeyFile` in `/etc/fwupd/redfish.conf` to
the path of a file containing the token:

```ini
[redfish]
SessionKeyFile=/run/example-bmc-auth/session
```

The file should live on tmpfs and be readable only by root so that no secret is
written to persistent storage. When the key is unset, or the file does not
exist, the plugin falls back to the configured username and password.

The session the file names expires independently of the daemon, so the file is
watched for changes and the key re-read when it is replaced. Replacing the file
is therefore enough to re-authenticate, with no need to restart `fwupd`. A file
that is removed, or replaced with one that is empty, leaves the previous key in
use.

## NVIDIA DGX Station GB300

A system is detected by its hardware IDs (CHIDs): when one has
`Flags = nvidia-oob` in `redfish.quirk`, the system is an NVIDIA OOB platform.
The quirk lists, for each OEM that builds the station, the CHID that
`fwupdtool hwids` shows for `Manufacturer + ProductName` (HardwareID-09).
NVIDIA's own station reports its product name as either `GB300 DGX Station` or
`DGX Station GB300`, so both of its CHIDs are listed. With no such entry, no
system is detected. Each entry in the BMC's
`FirmwareInventory` -- for example
`/redfish/v1/UpdateService/FirmwareInventory/FW_BMC_0` -- is then exposed as its
own device.

The vendor IDs of these devices are `DMI:` and `OEM:` followed by the system's
brand, the SMBIOS manufacturer. No `REDFISH:` vendor ID is added, as the
inventory `Manufacturer` is NVIDIA on every station. On an OEM-branded station a
firmware release therefore only matches the LVFS account of the OEM that sold the
system, never NVIDIA's.

A matching BMC gets GB300-specific update semantics, which diverge from DMTF
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
* When both the task monitor and `/Tasks/<id>` have been reaped, the
  resource-gone condition is treated as a completed update.

This device requires a session token, so see **Session Token Authentication**
above before installing firmware:

```shell
fwupdmgr get-devices        # lists FW_BMC_0, FW_GPU_0, FW_CPU_0, ...
fwupdmgr install firmware.cab --allow-reinstall
```

A full PLDM bundle flash takes about 20 minutes. The firmware is staged rather
than applied, and the device is marked as needing activation: it becomes active
only after an aux-rail power cycle. fwupd ends the install with its generic
request to unplug the power cable and plug it back in, which cycles the rail;
the BMC can be asked to do the same with:

```shell
fwupdmgr activate
```

This POSTs `{"ResetType":"AuxPowerCycleForce"}` to the OEM
`NvidiaChassis.AuxPowerReset` action advertised by `/redfish/v1/Chassis/BMC_0`,
which is not the chassis used to detect the GB300. The `Force` variant does not
wait for the host to shut down, so the system loses power as soon as the BMC
accepts the request -- save your work before activating.

### Design notes

* **Detection.** The Redfish root `Vendor` is not a usable signal, as OEMs also
  sell the Station and their BMCs report their own brand rather than NVIDIA.
  Other Redfish implementations must still not receive the GB300 update
  semantics above, so the system's hardware ID is matched against the quirk file,
  as fwupd does for other hardware. The BMC is not asked: the hardware IDs come
  from the host the daemon runs on, and there are none where there is no SMBIOS,
  such as a virtual machine, so such a system keeps the generic device.
* **Restricting a release to a vendor's systems.** The `DMI:` and `OEM:` vendor
  IDs are taken from the SMBIOS manufacturer, as other plugins do, so a release
  is only offered to a system whose brand the uploading LVFS account covers. The
  `REDFISH:` vendor ID the base probe takes from the inventory, and the `PCI:` one
  it takes from a related PCIe function, are suppressed with the
  `no-redfish-vendor-id` flag: they are NVIDIA on every station, and would let
  NVIDIA's releases reach an OEM-branded one. As
  the instance IDs are the same on every OEM's station, the devices also set
  `enforce-requires`: the release must list the system's CHID in a
  `<requires><hardware>` element, which narrows it to the systems it is for.
* **Board variants.** The GPU comes in variants, such as different HBM
  capacities, that take different images from the same bundle; the BMC picks one
  by the board's PCI IDs, and so does the variant instance ID above. The inventory
  entry relates to the GPU's chassis rather than its PCIe device, so the
  `related-chassis-pcie` flag makes the base probe follow the chassis's PCIe
  devices to their functions. It is not done by default, as a chassis can be the
  whole system, whose PCIe devices belong to other components.
* **Task polling.** A failed poll is tolerated when the error is retryable
  (`FWUPD_ERROR_BUSY`), so a brief connectivity blip during a flash of tens of
  minutes does not abandon a running update. The failure counter is reset by every
  poll that returns a valid task body, so only consecutive failures abort.
* **Pending activation.** Each time a device is probed the BMC's OEM slot data
  is consulted, so a pending activation survives a daemon restart, a host reboot
  and a same-version reinstall, none of which the engine's history can express.
  `FirmwareState` alone is not enough: the BMC also reports `PendingActivation`
  for a slot holding no image, after an update erased it and failed to
  authenticate the replacement, and acting on that would ask for an aux-rail
  power cycle that activates nothing.
* **Activation message.** The post-install request uses fwupd's generic
  `replug-power` message rather than custom text. Unplugging the power cord
  cycles the aux rail too, so the generic message is accurate, and fwupd hides
  the releases of a device whose requests need custom text from clients that
  cannot show it, such as `fwupdmgr` run from a script.
* **Firmware size.** The PLDM bundle is an opaque blob of about 117MB, above the
  `FuFirmware` parse ceiling, so it is read straight into a `FuFirmware` without
  parsing. The daemon enforces the limit set in `probe()`.
* **Which component is flagged.** The bundle is uploaded with an empty `Targets`
  and the BMC picks the components from the PLDM manifest, so the device a CAB
  addressed is usually not the one left pending -- a bundle targeting `FW_BMC_0`
  routinely stages `FW_CPU_0`. The plugin therefore asks the BMC which slots
  report `PendingActivation` and flags those, so `fwupdmgr activate` is pointed at
  the right component. Inside a composite install this runs from
  `composite_cleanup()`, once every component has been processed, as fwupd
  refuses to install onto a device that is already waiting for activation; until
  then the backend remembers the device whose write uploaded the bundle.

### Platform archives

A PLDM bundle is a single payload that the BMC fans out across every component
in its manifest, so an archive declares **one component per firmware image**,
each targeting that component's own device at its own version, all sharing one
`firmware.bin`. Two shipping GB300 bundles carry an identical BMC image and
differ only in the SBIOS, so naming an archive after any one component would
make the other invisible.

Declaring components separately is also what lets an archive carry any subset of
what a board has -- a bundle may ship images for hardware the board does not
have, and a board may have components the bundle does not touch -- and lets
components move in different directions within one release. A single version
could express neither.

The plugin uploads the payload once per archive. `fu_engine_install_releases()`
is a plain loop with no dedupe, so without this the same ~117MB bundle would be
POSTed once per matching component. The checksum of the last uploaded payload is
remembered, and a component presenting the same bytes skips the POST once the
BMC confirms its slot is staged. Keying on the payload rather than on a
transaction is also correct across invocations: re-installing the same archive
before activating does not resend it, and a stale `PendingActivation` from an
unrelated bundle cannot suppress a genuine install, because its checksum
differs.

Because the components move together as a qualified set rather than as
independent upgrades, `fwupdmgr sync` against a Best Known Configuration tag is
the natural way to apply one: it installs anything not already at the tagged
version, in either direction.

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
