---
title: Plugin: NVIDIA OOB Redfish
---

## Introduction

This plugin updates firmware on NVIDIA out-of-band (OOB) managed components
through the platform Baseboard Management Controller (BMC) using the Redfish
`UpdateService`. It was written for the NVIDIA Galaxy GB300 reference platform,
where the BMC exposes a Redfish `FirmwareInventory` covering components such as
the BMC firmware, System BIOS (SBIOS), ERoT devices and the NVIDIA HPM PLDM
firmware bundle (GPU/CX9/FPGA/MCU).

Unlike the generic `redfish` plugin, this plugin consumes a pre-established
Redfish session token rather than authenticating in-process, and it only
registers inventory members that the BMC reports as updatable. It declares a
conflict with the generic `redfish` plugin so that it owns the BMC inventory
exclusively on supported platforms.

## Firmware Format

The daemon will decompress the cabinet archive and extract a firmware blob in
an unspecified binary file format. The blob is pushed to the BMC using the
Redfish `MultipartHttpPushUri` advertised by the `UpdateService`, and the
resulting Redfish `Task` is polled until it completes.

This plugin supports the following protocol ID:

* `org.dmtf.redfish`

## GUID Generation

Each Redfish `FirmwareInventory` member becomes one device. These devices use
the following instance IDs:

* `NVIDIA_OOB\URI_${InventoryUri}` — always emitted; unique per Redfish
  `FirmwareInventory` entry and used to disambiguate components that share a
  `SoftwareId`.
* `NVIDIA_OOB\SWID_${SoftwareId}` — emitted when the BMC reports a
  `SoftwareId`; components that share a `SoftwareId` intentionally hash to the
  same GUID to support unified-image (PLDM bundle) update flows.
* `NvidiaOob\Name=${Name}` — quirk-matching instance ID used by
  `nvidia-oob-redfish.quirk`.

## Update Behavior

The firmware is uploaded to the BMC, which schedules the actual write. The
Redfish API does not specify when the firmware is written to the target device,
so the plugin polls the returned `Task` until it reports completion. Components
that require a manual AC (power) cycle are flagged so the operator is informed.

## Vendor ID Security

The vendor ID is set from the DMI data, in this instance set to `DMI:NVIDIA`
and `OEM:NVIDIA`.

## Quirk Use

This plugin uses the following plugin-specific quirks:

### `Summary`

Provides a friendly summary string for BMC-reported inventory names. Matched
by the `NvidiaOob\Name=` instance ID.

## Configuration

The optional configuration file is installed to `/etc/fwupd/nvidia_oob.conf`.
When present, its values override the compiled-in defaults and take effect at
daemon startup (restart fwupd after editing). The following keys are supported
under the `[OOB]` section:

* `BmcHost` — BMC Redfish base address (default `10.0.1.1`).
* `BmcUser` — BMC login username (default `root`).
* `CaPath` — PEM-encoded CA bundle used to verify the BMC TLS certificate
  (default `/etc/fwupd/pki/nvidia-oob/ca.pem`).
* `TaskTimeout` — maximum time in seconds to poll a Redfish `Task` before
  giving up (default `3600`).

The BMC password is never stored in the configuration file. The Redfish
session token is read from the `NVIDIA_OOB_TOKEN` environment variable or from
`/run/fwupd/nvidia_oob.session` (tmpfs, mode `0600`, cleared on reboot), which
is provisioned out-of-band by the platform authentication helper.

## External Interface Access

This plugin requires HTTP(S) access to the BMC Redfish service (by default over
the platform's USB virtual NIC).

## Version Considerations

This plugin has been available since fwupd version `2.0.0`.
