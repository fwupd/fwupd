# lxs-touch

## Introduction

This plugin supports firmware updates for the LX Semicon SW42101 touch controller.
It communicates over the HID RAW interface (hidraw) and handles two protocol modes: SWIP and DFUP.

### Protocol Modes

- **SWIP (Software In-Protocol)**: Normal touch operation mode. Version and panel information can be queried.
- **DFUP (Device Firmware Upgrade Protocol)**: Bootloader mode dedicated to firmware updates.

### Firmware Update Flow

1. **Setup**: On device connection, check the protocol mode and read version and panel information.
2. **Detach**: Switch from SWIP mode to DFUP mode (`REG_CTRL_SETTER`, mode=`0x02`) and wait for replug.
3. **Write**: Write firmware to flash in chunks while in DFUP mode.
4. **Attach**: After firmware write completes, restart the device via watchdog reset (`CMD_WATCHDOG_RESET`).

### Flash Write Modes

| Mode   | Chunk size | Transmit unit | Verification     |
|--------|------------|---------------|------------------|
| Normal | 128 bytes  | 16 bytes      | None             |
| 4K     | 4096 bytes | 48 bytes      | Yes (5 retries)  |

4K mode support is auto-detected at runtime by querying the device (`CMD_FLASH_4KB_UPDATE_MODE`).

## Firmware Formats

| Type               | Size  | Flash offset | Description                   |
|--------------------|-------|--------------|-------------------------------|
| Application-only   | 112KB | `0x4000`     | Application area only         |
| Boot + Application | 128KB | `0x0000`     | Bootloader + full application |

## Version Format

The version consists of four fields in hex: `{boot_ver}.{core_ver}.{app_ver}.{param_ver}`

Example: `00A1.00B2.00C3.00D4`

## Communication Protocol

Uses HID Output Reports (Report ID `0x09`, 64-byte buffer).

Read response header is 4 bytes; payload data starts at offset 4.

## Vendor ID Security

The vendor ID is set from the HID vendor ID, in this instance set
to `HIDRAW:0x1FD2`.

## External Interface Access

This plugin requires ioctl `HIDIOCSFEATURE` and `HIDIOCGFEATURE` access.

## Version Considerations

This plugin has been available since fwupd version `2.1.2`.
