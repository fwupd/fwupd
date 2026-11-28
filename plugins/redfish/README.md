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
if provided, along with one instance ID for each class and target pair:

* `REDFISH\VENDOR_HPE&CLASS_${HpeDeviceClass}&TARGET_${HpeTarget}`

Both are lower case. A target can be shared with other devices of the same board and a class with
other platforms, so only the pair identifies a device.

If the `SoftwareId` property is not a GUID then the vendor instance ID is used instead:

* `REDFISH\VENDOR_${RedfishManufacturer}&SOFTWAREID_${RedfishSoftwareId}`

Additionally, on Dell hardware the SystemID is also used:

* `REDFISH\VENDOR_${RedfishManufacturer}&SYSTEMID_${RedfishSystemID}&SOFTWAREID_${RedfishSoftwareId}`
* `REDFISH\VENDOR_${RedfishManufacturer}&SYSTEMID_${RedfishSystemID}&SOFTWAREID` (only-quirks)

Additionally, this Instance ID is added for matching, quirks and parent matching:

* `REDFISH\VENDOR_${RedfishManufacturer}&ID_${RedfishId}`

On the NVIDIA DGX Station GB300 each device is a child of the BMC, which is the inventory member
that the manager reports as its `ActiveSoftwareImage`. A component that comes in board variants
also gets this Instance ID for each PCIe function of its board, read from the PCIe devices of its
related chassis:

* `REDFISH\VENDOR_${RedfishManufacturer}&ID_${RedfishId}&VEN_${PciVendor}&DEV_${PciDevice}&SUBSYS_${PciSubsysVendor}${PciSubsys}`

## Update Behavior

The firmware will be deployed as appropriate. The Redfish API does not specify
when the firmware will actually be written to the SPI device.

On the NVIDIA DGX Station GB300 the BMC stages the firmware, and it becomes active at the next AC
power cycle. The components that the BMC reports as `PendingActivation` need activation, either by
unplugging the power cable or with `fwupdmgr activate`, which asks the BMC to shut the host down and
cut and restore the power of the whole board. Until then, the other components are only updated
when a release is named explicitly, so that `fwupdmgr update` does not upload the bundle again.

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

### RedfishDeviceGType

The device type to create for each inventory member on a system matched by a CHID, e.g.
`FuRedfishHpeDevice`. It replaces the type the plugin would otherwise choose from the update
service and the vendor, which allows a vendor-specific device to be used on an OEM-branded system.

Since: 2027.0

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

### `Flags=related-chassis-pcie`

Read the PCI IDs from the PCIe devices of a related chassis, and add the vendor and `Id` Instance ID
with the PCI IDs of each function. This is only correct when the chassis holds a single component
rather than the whole system.

Since: 2027.0

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

## Bearer Token Rotation

The `[redfish]` `BearerToken` setting is reloaded when `fwupd.conf` changes, so
a rotated token does not need a daemon restart. Removing the setting restores
username/password authentication, so `Username` and `Password` must be configured
first: a daemon started with a token does not create a BMC user. Changes to them
still need a restart, and a `BearerToken` in the mutable configuration overrides
the system value, so rotate or remove it there too.

## Session Key Rotation

The file named by the `[redfish]` `SessionKeyFile` setting is read for each request,
so a session that is replaced out of band does not need a daemon restart. A request
made while the file cannot be read fails, rather than using other credentials.

## External Interface Access

This requires HTTP access to a given URL.

## Version Considerations

This plugin has been available since fwupd version `1.1.0`.
