---
title: Plugin: NVIDIA OOB
---

## Introduction

This plugin updates the out-of-band firmware of NVIDIA platforms whose BMC applies a
whole PLDM firmware bundle at once, starting with the NVIDIA DGX Station GB300. The
components downstream of the BMC can only be updated by it, so the plugin hands the
bundle to the BMC over Redfish and tracks what the BMC stages.

A system is matched by its hardware IDs (CHIDs): when one has `Flags = nvidia-oob` in
`nvidia-oob.quirk`, the plugin takes over the BMC and disables the generic `redfish`
plugin, so the same BMC is not driven twice. The quirk lists, for each OEM that builds
such a system, the CHID that `fwupdtool hwids` shows for `Manufacturer + ProductName`
(HardwareID-09). The BMC is not asked, as an OEM-branded BMC reports its own brand.

## Firmware Format

The daemon will decompress the cabinet archive and extract a firmware blob, a DMTF PLDM
firmware update package (DSP0267). It is passed to the BMC unparsed.

A bundle carries one image for each component. An archive therefore declares one
component per image, each targeting its own device at the version of that image, all
sharing the same `firmware.bin`. `fwupdmgr get-updates` then lists exactly the
components a release changes, and a release can move components in different
directions.

This plugin supports the following protocol ID:

* `org.dmtf.redfish`

## GUID Generation

Each `FirmwareInventory` member is a device, matched by its inventory `Id`, e.g.

* `REDFISH\ID_FW_BMC_0`

The ID carries no manufacturer, which differs between OEM-branded and NVIDIA-branded
images on the same system, and not the `SoftwareId`, which several components share.

A component that comes in board variants, such as the GPU, also gets the PCI IDs of the
function listed under its related chassis, e.g.

* `REDFISH\ID_FW_GPU_0&VEN_10DE&DEV_31C2&SUBSYS_10DE20E5`

These IDs are the same on every OEM's system, so the devices set `enforce-requires`: a
release must also list the CHIDs of the systems it is for.

## Update Behavior

The bundle is POSTed to the BMC's `MultipartHttpPushUri` with an empty `Targets` array,
`ForceUpdate` and `@Redfish.OperationApplyTime` set to `Immediate`; the BMC picks the
components from the bundle itself. The bundle is uploaded once per checksum rather than
once per component of an archive. The task is polled on `/redfish/v1/TaskService/Tasks/<id>`,
where `PercentComplete` is reported; once the task completes its monitor answers HTTP 200
with an empty body, which counts as completion.

The BMC only stages the firmware, which takes effect at the next aux power cycle. The
components that report `PendingActivation` with a version in
`Oem.Nvidia.ActiveFirmwareSlot` are marked as needing activation, or the device written
when none do, and every updatable component gets `only-explicit-updates`, so
`fwupdmgr update` offers a bundle once. Inside a composite install this happens once
every component has been processed. The slots are read at every probe, so the state
survives a daemon restart.

`fwupdmgr activate` POSTs `ResetType=AuxPowerCycleForce` to the
`NvidiaChassis.AuxPowerReset` action advertised by `/redfish/v1/Chassis/BMC_0`. The
install ends with the generic request to unplug the power cable, which cycles the aux rail
too. A reboot or a soft power-off does not, so neither is requested.

## Vendor ID Security

The vendor IDs are `DMI:` and `OEM:` followed by the system's SMBIOS manufacturer, so an
OEM-branded system only takes releases from the LVFS account of the OEM that sold it.

## Configuration

The BMC is configured in the `[nvidia_oob]` section of `fwupd.conf`:

* `Uri`: the BMC Redfish service, e.g. `https://192.0.2.1`
* `SessionKeyFile`: a root-only file holding a Redfish `X-Auth-Token`, re-read whenever
  it is replaced, so no password needs to be stored
* `Username` and `Password`: used when no session key file is set
* `CACheck`: verify the BMC certificate, `false` by default

## External Interface Access

This plugin requires access to the BMC Redfish service over the network.

## Version Considerations

This plugin has been available since fwupd version `2.2.1`.

## Owners

Anyone can submit a pull request to modify this plugin, but the following people should be
consulted before making major or functional changes:

* NVIDIA: @vraghav8
