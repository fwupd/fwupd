---
title: Plugin: FocalTech Fingerprint Sensor
---

## Introduction

This plugin updates firmware for FocalTech Match-On-Chip (MOC) fingerprint
sensors over USB bulk transfer.

The communication protocol is derived from the reference `UpgradeTool` source
code provided by FocalTech Systems Co., Ltd.

## Protocol

Every transaction uses a simple framed packet format:

```
| 0x02 | LEN_HI | LEN_LO | CMD | data... | BCC |
```

| Field | Size | Description |
|-------|------|-------------|
| magic | 1 B  | Always `0x02` |
| LEN   | 2 B  | Big-endian uint16; value = (data bytes) + 1 (for trailing BCC) |
| CMD   | 1 B  | Command byte |
| data  | 0–N B | Command-specific payload |
| BCC   | 1 B  | XOR of LEN_HI through the last data byte |

The device always responds with `CMD = 0x04` (ACK).  The first data byte of
the ACK (`data[0]`) is a status code: `0x01` = success.

### Command Table

| CMD  | Name              | Direction | Description |
|------|-------------------|-----------|-------------|
| 0xC0 | GET_CONNECT_STATUS| H → D     | Check module connectivity |
| 0xD0 | GET_FW_VERSION    | H → D     | Read current firmware version string |
| 0xD1 | ENTER_BOOT_MODE   | H → D     | Switch device to bootloader mode |
| 0xD2 | FW_DATA_WRITE     | H → D     | Write one firmware block (payload = block_idx (u16be) + data) |
| 0xD3 | FW_CHECKSUM       | H → D     | Ask device to verify the written image |
| 0xD4 | FW_RESET          | H → D     | Reset / reboot into new firmware |

### Firmware Update Sequence

```
1. GET_FW_VERSION  → read current version (for fwupd version comparison)
2. ENTER_BOOT_MODE → switch device to bootloader
   (device re-enumerates — wait for replug)
3. FW_DATA_WRITE × N → transfer firmware in 1 KiB blocks
                        payload per block: [ block_index(u16be) | 1024 bytes ]
4. FW_CHECKSUM     → device verifies the written image integrity
5. FW_RESET        → reboot into new firmware
   (device re-enumerates — wait for replug)
```

## Firmware Format

The daemon decompresses the cabinet archive and extracts a flat binary firmware
blob.  No additional wrapping is expected; the raw binary is split into 1 KiB
blocks and written sequentially.

This plugin supports the following protocol ID:

* `com.focaltech.moc`

## GUID Generation

These devices use the standard USB DeviceInstanceId values.  Because the quirk
matches on VID only (`USB\VID_2808`), **any PID** under vendor 0x2808 is
handled automatically — no quirk change is required when a new product is
released.

The per-device GUID used by the `.cab` firmware package is derived from the
full `USB\VID_2808&PID_XXXX` string.  Run:

```shell
fwupdmgr get-devices
```

on a system with the sensor connected to obtain the exact GUID for your
hardware, then put that GUID into the `<provides>` section of the firmware
`metainfo.xml`.

## Update Behavior

The firmware is deployed when the device is in normal runtime mode.  The plugin
first switches the device to bootloader mode (`detach`), writes the firmware,
verifies the checksum, then sends a reset command (`attach`).  The device
re-enumerates after each mode switch.

## Vendor ID Security

The vendor ID is set from the USB vendor ID, in this instance `USB:0x2808`.

## External Interface Access

This plugin requires read/write access to `/dev/bus/usb`.

## Version Considerations

This plugin has been available since fwupd version `2.0.0`.
