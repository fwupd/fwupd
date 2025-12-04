---
title: Plugin: PixArt Touchpad
---

## Introduction

The **PixArt Touchpad** plugin (`pixart_tp`) updates PixArt touchpad firmware
that enumerates as **HID** devices (either **USB-HID** or **I²C-HID** exposed via
`hidraw`). The device can enter a lightweight bootloader (“engineer mode”)
and is then flashed over the same HID interface.

This plugin sets the fwupd device protocol to **`com.pixart.tp`** and reports
version numbers in **hex** format.

## Firmware Format

The update payload is shipped as a **CAB** that contains:

- `firmware.metainfo.xml` — metadata and release info
- `firmware.bin` — a PixArt container with magic **`FWHD`** (header v1.0)

The container is parsed by `FuPxiTpFirmware` and validated using:

- **Header CRC32** (over the header minus CRC field)
- **Payload CRC32** (over the bytes after the header)

Each updateable **internal** section defines a flash start address and a
file offset/length. The plugin programs flash in **4 KiB sectors** with
**256-byte pages** via a small SRAM window.

This plugin supports the following protocol ID:

- `com.pixart.tp`

## GUID Generation

These devices use the standard **HID** DeviceInstanceId values, e.g.

- `HIDRAW\VEN_093A&DEV_0343`

> Note: If the same silicon enumerates as a USB interface on some systems,
> an additional USB GUID like `USB\VID_093A&PID_0343` may also be provided
> in the firmware metadata. GUIDs derived from hardware IDs are **stable
> across machines**.

## Update Behavior

High-level flow:

1. **Detach (enter bootloader)**  
   Writes device registers to switch to engineer mode:
   - `bank 0x01, reg 0x2c = 0xaa`
   - `bank 0x01, reg 0x2d = 0xcc`

2. **Erase/Program**  
   - Erase flash by **sector (4 KiB)**  
   - Program by **page (256 B)** using an SRAM selected by `SramSelect`
   - Busy/write-enable checks are performed between operations

3. **Attach (exit bootloader)**  
   - `bank 0x01, reg 0x2c = 0xaa`
   - `bank 0x01, reg 0x2d = 0xbb`

4. **Reload (rebind transport)**  
   The plugin triggers a **driver rebind** on the nearest parent
   (`hid`, `i2c`, or `usb`) by writing to
   `/sys/bus/<subsystem>/drivers/<driver>/{unbind,bind}` and sets
   `WAIT_FOR_REPLUG` so fwupd waits for re-enumeration and fetches the
   **new HID report descriptor**.

No OS reboot is required.

## Vendor ID Security

The vendor ID is derived from the HID layer; in this instance it will appear as:

- `HIDRAW:0x093A`

(If a USB parent is present, fwupd may also expose the USB VID/PID, but the
primary identity for this plugin is the HID instance.)

## Quirk Use

This plugin supports the following **plugin-specific** quirks (set in
`/usr/share/fwupd/quirks.d/*.quirk`):

### `HidVersionReg`

Defines where to read the **device firmware version** (2 bytes, **little-endian**):

- `bank` : 8-bit bank/index
- `addr` : 16-bit base address; the plugin reads `addr+0` (lo) and `addr+1` (hi)

**Defaults** (can be overridden by quirk): `bank=0x00`, `addr=0x0b`.

**Example** (your current test unit):

[HIDRAW\VEN_093A&DEV_0343]
Plugin = pixart_tp
GType = FuPxiTpDevice
HidVersionReg = bank=0x00; addr=0xb2

### `SramSelect`

Selects the SRAM type used for 256-byte page programming:
SramSelect = 0x0f

**Default**: `0x0f`.

## External Interface Access

This plugin requires:

- Read/write access to **`/dev/hidraw*`**
- Permission to write **sysfs driver control** files for rebind:  
  `/sys/bus/<hid|i2c|usb>/drivers/<driver>/{unbind,bind}`

fwupd runs as root on typical installations, which is sufficient.

## Version Considerations

This plugin will be available in fwupd. (first release after merge).
Until then, build from source and run with:

```bash
fwupdtool --plugins pixart-tp --verbose get-devices
fwupdtool --plugins pixart-tp --verbose install ./pixart-tp-<ver>.cab \
  --allow-older --allow-reinstall
```

## Owners

Anyone can submit a pull request to modify this plugin, but the following people should be
consulted before making major or functional changes:

- <harris_tai@pixart.com>
- <micky_hsieh@pixart.com>
