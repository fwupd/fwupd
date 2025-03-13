---
title: Plugin: Qc Firehose
---

## Introduction

Qualcomm MSM based devices contain a special mode of operation, called Emergency Download Mode.
In this mode, the device identifies itself as Qualcomm HS-USB 9008 through USB, and can communicate
with a PC host. EDL is implemented by the SoC ROM code (also called PBL).

The EDL mode itself implements the Qualcomm Sahara protocol, which accepts an OEM-digitally-signed
payload over USB.

## Firmware Format

The daemon will decompress the cabinet archive and then extract a zip payload. Inside this payload
there must be:

* One saraha binary with the `.mbn` extension that is suitable for the target platform
* One manifest file matching `rawprogram*.xml` that has sections with `<program>` and `<erase>`
* Each file referenced by the manifest must also be included

This plugin supports the following protocol ID:

* `com.qualcomm.firehose`

## GUID Generation

These devices use the standard USB DeviceInstanceId values, e.g.

* `USB\VID_05C6&PID_9008`

## Update Behavior

The Firehose protocol is really two protocols in a trenchcoat.

When the device is plugged in, **it** sends a `Hello` *Sahara* protocol packet which is then
acknowledged by the host. The device then requests the sahara binary blob which the host chunks up
and sends to the device at the offset requested.

Once completed the *first* write phase is complete and we signal to the daemon that another write
is required. We then switch to the *mostly* XML-based protocol called Firehose.

The plugin then sends a default suggested configuration in XML format to the device, which then
sends back what it actually wants. Then the plugin opens the XML manifest and processes each
`<erase>`, `<program>` and `<patch>` section in the specific order.

Any `<program>` is also loaded from the archive, but is sent in binary using the *Sahara* protocol
rather than with *Firehose*.

Finally the device is rebooted, again using Firehose, returning the device to runtime "modem" mode.

## Vendor ID Security

The vendor ID is set from the USB vendor, in this instance set to `USB:0x05C6`

## Quirk Use

This plugin uses the following plugin-specific quirks:

### `Flags=loaded-sahara`

The device has loaded the Sahara binary.

Since: 2.0.7

### `Flags=no-zlp`

The device should not have the Zero Length Packet sent.

Since: 2.0.7

## External Interface Access

This plugin requires read/write access to `/dev/bus/usb`.

## Version Considerations

This plugin has been available since fwupd version `2.0.7`.

## Owners

Anyone can submit a pull request to modify this plugin, but the following people should be
consulted before making major or functional changes:

* Richard Hughes: @hughsie
