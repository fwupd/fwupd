# lxs-touch

## Introduction

This plugin supports firmware updates for the LX Semicon SW42101 touch controller.
It communicates over the HID RAW interface (hidraw) and handles two protocol modes: SWIP and DFUP.

## Supported Devices

| Mode | VID | PID | Description |
|------|-----|-----|-------------|
| SWIP (normal) | `0x1FD2` | `0xB011` | Normal operation mode (SW42101) |
| DFUP (update) | `0x29BD` | `0x5357` | Bootloader/update mode |
| DFUP (update, alt) | `0x1FD2` | `0x5357` | Bootloader/update mode (alt VID) |

## How It Works

### Protocol Modes

- **SWIP (Software In-Protocol)**: Normal touch operation mode. Version and panel information can be queried.
- **DFUP (Device Firmware Upgrade Protocol)**: Bootloader mode dedicated to firmware updates.

### Firmware Update Flow

1. **Setup**: On device connection, check the protocol mode and read version and panel information.
2. **Detach**: Switch from SWIP mode to DFUP mode (`REG_CTRL_SETTER`, mode=`0x02`) and wait for replug.
3. **Write**: Write firmware to flash in chunks while in DFUP mode.
4. **Attach**: After firmware write completes, restart the device via watchdog reset (`CMD_WATCHDOG_RESET`).

### Flash Write Modes

| Mode | Chunk size | Transmit unit | CRC verification |
|------|-----------|---------------|------------------|
| Normal mode | 128 bytes | 16 bytes | None |
| 4K mode | 4096 bytes | 48 bytes | Yes (up to 5 retries on failure) |

4K mode support is auto-detected at runtime by querying the device (`CMD_FLASH_4KB_UPDATE_MODE`).

## Firmware Formats

| Type | Size | Flash offset | Description |
|------|------|-------------|-------------|
| Application-only | 112 KB (`0x1C000`) | `0x4000` | Application area only |
| Boot + Application | 128 KB (`0x20000`) | `0x0000` | Bootloader + full application |

## Version Format

The version consists of four fields:

```
{boot_ver}.{core_ver}.{app_ver}.{param_ver}
```

Example: `1.2.3.4`

## Communication Protocol

Uses HID Output Reports (Report ID `0x09`, 64-byte buffer).

### Packet Structure (`FuStructLxsTouchPacket`)

| Field | Size | Description |
|-------|------|-------------|
| `report_id` | 1 byte | Always `0x09` |
| `flag` | 1 byte | `0x68` = Write, `0x69` = Read |
| `length_lo` | 1 byte | Data length low byte |
| `length_hi` | 1 byte | Data length high byte |
| `command_hi` | 1 byte | Register address high byte |
| `command_lo` | 1 byte | Register address low byte |
| `data` | N bytes | Payload (appended via `memcpy` in C) |

Read response header is 4 bytes; payload data starts at offset 4.

### Key Register Addresses

| Register | Address | Description |
|----------|---------|-------------|
| `REG_INFO_PANEL` | `0x0110` | Panel resolution and node info |
| `REG_INFO_VERSION` | `0x0120` | Firmware version info |
| `REG_INFO_INTEGRITY` | `0x0140` | Integrity info |
| `REG_INFO_INTERFACE` | `0x0150` | Protocol mode identification |
| `REG_CTRL_GETTER` | `0x0600` | Ready status query |
| `REG_CTRL_SETTER` | `0x0610` | Operating mode control |
| `REG_CTRL_DFUP_FLAG` | `0x0623` | DFUP flag |
| `REG_FLASH_IAP_CTRL_CMD` | `0x1400` | Flash IAP command |
| `REG_PARAMETER_BUFFER` | `0x6000` | Data transmit buffer |

## File Layout

| File | Description |
|------|-------------|
| `fu-lxs-touch-plugin.c` | Plugin registration, hidraw subsystem and GType registration |
| `fu-lxs-touch-device.c` | Device logic (setup, detach, write, attach) |
| `fu-lxs-touch-firmware.c` | Firmware file parsing, size and offset determination |
| `fu-lxs-touch.rs` | Protocol constants and packet struct definitions (Rust struct-generator) |
| `lxs-touch.quirk` | Supported device VID/PID and metadata quirk entries |
| `meson.build` | Build configuration |

## Building

This plugin is Linux-only (`host_machine.system() == 'linux'`) and is automatically included in the fwupd build system.

```bash
# Test with only this plugin loaded
fwupdtool --plugins lxs_touch get-devices --verbose
```
