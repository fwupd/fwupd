---
title: Plugin: VBE — Verified Boot for Embedded
---

## Introduction

This plugin is used for boards which use the VBE system. This allows the
platform to be updated from user space.

## Firmware Format

Firmware updates are held within a CAB file, a Windows format which allows
files to be collected and compressed, similar to a tar file. The CAB file can be
created using the `fwupdtool` tool, as shown in the VBE `example` directory.

This section describes the format used by vbe-simple. vbe-abrec uses the image
for one slot instead (see below).

Inside the CAB file is the firmware itself, in Flat Image Tree (FIT) format.
This is typically called `firmware.fit` and can be created by the `mkimage` tool,
or using device tree tools such as `dtc` and `fdtput`.

The FIT supports multiple configurations, each of which is intended to update a
particular board type. This allows the same firmware update to be used for a
number of related boards, including sharing certain images, at the expense of
increasing the update size.

A typical FIT file looks like this:

```fdt
/ {
    timestamp = <0x62a74c6c>;
    description = "Firmware image with one or more FDT blobs";
    creator = "U-Boot mkimage 2021.01+dfsg-3ubuntu0~20.04.4";
    #address-cells = <0x00000001>;
    images {
        firmware-1 {
            description = "v1.2.4";
            type = "firmware";
            arch = "arm64";
            os = "u-boot";
            compression = "none";
            store-offset = <0x10000>;
            data = <...>;
            hash-1 {
                value = <0xa738ea1c>;
                algo = "crc32";
            };
        };
    };
    configurations {
        default = "conf-1";
        conf-1 {
            version = "1.2.4";
            compatible = "pine64,rockpro64-v2.1", "pine64,rockpro64";
            firmware = "firmware-1";
        };
    };
};
```

Each configuration includes the version of the update, the board(s) it is
compatible with and a list of firmware 'images' in the `firmware` property.
The `compatible` property is optional in the case where there is only one
configuration that applies to all boards that receive this update.

The images themselves are in a separate `images` node. Each image includes
various fields to indicate its type as well as optional hashes. The `data`
property contains the actual firmware data.

A configuration can list more than one image in its `firmware` property, for
example when the firmware is in several parts which are kept at different places
in the storage. fwupd writes each image at the position given by its
`store-offset` property, an offset in bytes from the start of the firmware area
(`area-start`, described below). The images must not overlap: fwupd does not
check this, so a later image would overwrite part of an earlier one. Each image
must also fit within the area (`area-size`). Without `store-offset` the image is
written at the start of the area, which suits a configuration with only one
image.

It is common to use an 'external' FIT, meaning that the `data` property is
omitted and the data is placed in the same file after the FIT itself. In this
case `data-offset` and `data-size` allow the data to be relocated. The data is
stored started at the next 32-bit aligned file position after the FIT. The
`mkimage` tool allows converting a FIT to an external FIT, with the `-E` flag.

See the `plugins/vbe/example/simple` directory for an example of building a
firmware update.

## Operation

The daemon decompresses the cabinet archive, extracts the firmware blob and
writes it to storage. The running firmware is not affected, since the new
firmware is only used from the next reboot. With vbe-simple the update
overwrites the only copy of the firmware. With vbe-abrec it is written to the
B slot, which the next reboot tries, falling back to A if it does not boot (see
below).

This plugin supports the following protocol ID:

* `org.vbe`

## Board information

VBE requires the board firmware to provide information about the firmware within
the device tree passed to the Operating System.

For systems without device tree, currently the only option is to install a file
for use by fwupd, typically in `/var/lib/fwupd/system.dtb`. Since the board's
instance ID comes from the `compatible` property of the `/chosen/fwupd` node,
the file should provide it.

In either case, there must be one or more nodes with the required information.
The format depends on which VBE method is used, but the information must be in
a subnode of `chosen/fwupd`. Here is an example:

```fdt
/ {
 compatible = "pine64,rockpro64-v2.1";
 chosen {
  fwupd {
   firmware {
    compatible = "fwupd,vbe-simple";
    cur-version = "1.2.3";
    storage = "/tmp/testfw";
    area-start = <0x100000>;
    area-size = <0x100000>;
    skip-offset = <0x8000>;
    part-uuid = "62db0ccf-03";
    part-id = "3";
   };
  };
 };
};
```

The first compatible string indicates the board type. Typically it is a single
string, but a list is also supported. VBE matches the string(s) here against
those in the update itself, using the configuration which has the earliest match
(within the list) to the board's compatible string(s). Since more specific
boards are at the start of the compatible string, this allows for one
configuration to have an update for a specific board, while another provides an
update for all other boards.

The compatible string of the firmware update indicates the VBE method that is
being used. It must have `fwupd` as the manufacturer and the model must start
with `vbe-`.

Other properties within the node are determined by that method, but some are
common to all:

* `compatible` - indicates the VBE method to use, in the form `fwupd,<method>`.
* `cur-version` - indicates the version that is currently installed, if this is
  known by the firmware. Note that fwupd keeps its own information about the
  installed version, so this is not needed by fwupd itself. But it can be useful
  for other utilities.

## Important files

* `/var/lib/fwupd/system.dtb` - this file holds the system
  information. It overrides the system device tree if any, meaning that the
  system device tree is ignored if this file is present. For systems that don't
  support device tree (e.g ACPI systems), this file is needed for VBE to work

## Available VBE methods

Note that all VBE methods must subclass `FuVbeDevice` since it provides access to
the device tree and the VBE directory, among other things.

These methods are available:

* `fwupd,vbe-simple` - writes a single copy of the firmware to media
* `fwupd,vbe-abrec` - keeps A, B and recovery copies, trying an update in B
  before using it

### vbe-simple

With this, only a single copy of the firmware is available so if the write
fails, the board may not boot. This is implemented in the `vbe-simple.c` file and
has the device GUID `bb3b05a8-ebef-11ec-be98-d3a15278be95` within fwupd, which
matches every board using this method. It also has an instance ID of the form
`VBE\COMPATIBLE_xxx`, using the first compatible string of the `/chosen/fwupd`
node, so that an update can be targeted at one board. fwupd's hardware IDs
(HWIDs) are another way to do this: fwupd makes them from the compatible strings
of the root node on a devicetree system, or from the SMBIOS information on other
systems, so an update can instead require the HWID of a particular board.

Properties for this method are:

* `compatible` - must be `fwupd,vbe-simple`
* `storage` - device to store firmware in. Two options are supported: a full path
   such as `/dev/mmcblk1` or a device number, like `mmc1`. Note that only mmc
   is currently supported.
* `area-start` - start offset in bytes of the firmware area within the storage
* `area-size` - size in bytes of the firmware area within the storage
* `skip-offset` - offset to preserve at the start of the firmware area. This
   means that the first part of the image is ignored, with just the latter part
   being written. For example, if this is 0x200 then the first 512 bytes of the
   image (which must be present in the image) are skipped and the bytes after
   that are written to the store offset.
* `part-uuid` - the UUID of the partition containing the fwupd state. This is not
   used by fwupd at present but may allow the firmware to check the fwupd
   state on boot
* `part-id` - the partition number containing the fwupd state. This is not
   used by fwupd at present but may allow the firmware to check the fwupd
   state on boot

### vbe-abrec

This keeps two or three copies of the firmware: A, B and optionally recovery. A
is the copy normally booted, B is used to try out an update and recovery is used
if requested (e.g. from the firmware's command line, see below) or if neither A
nor B can be loaded. Recovery is never written by fwupd. Installing an update
clears any recovery request, so that the update is tried in B. Request recovery
again afterwards if it is still wanted.

An update is written to the B slot. fwupd then sets the state so that the
firmware tries B, up to three times. Each attempt uses up a try. When the OS
starts after booting B, the firmware reports `booted-slot = "b"`, so fwupd
copies B to A and clears the request. From then on A holds the new firmware. If
B runs out of tries without reaching the OS, the firmware marks it bad and boots
A, so the update has failed and the version is unchanged.

This relies on a watchdog: if B hangs before the OS starts, the watchdog resets
the board, so the next boot uses up another try. Without one, a B which hangs
stops the board until it is power-cycled.

The update payload is the complete image for one slot, as built for the board.
It must start with a FIT and fit below `slot-info-offset`.

As with vbe-simple, the device has an instance ID of the form
`VBE\COMPATIBLE_xxx`, using the first compatible string of the `/chosen/fwupd`
node, which the firmware copies from the board's root node. This allows an
update to be targeted at a particular board.

Properties for this method are:

* `compatible` - must be `fwupd,vbe-abrec`
* `storage` - device to store firmware in, as for vbe-simple
* `area-start` - start offset in bytes of the VBE area within the storage
* `state-offset` - offset of the state from `area-start`, in bytes
* `state-size` - size of the state, a power of two from 0x10 to 0x40
* `slot-a-offset`, `slot-b-offset` - offsets of the A and B firmware in the
   storage
* `slot-recovery-offset` - offset of the recovery firmware, if there is any;
   fwupd never writes it, but checks that A, B and the state do not overlap it
* `slot-size` - space available for each copy of the firmware
* `slot-info-offset` - offset of the slot info within each slot; the firmware
   must fit below this
* `slot-info-size` - space for the slot info, from 0x100 to 0x10000 bytes
* `booted-slot` - added by the firmware to say which copy was booted: `a`, `b`
   or `recovery`. If this is missing, fwupd uses the pick which the firmware
   records in the state
* `cur-version` - added by the firmware, though fwupd reads the version from the
   slot info of A

The state is `state-size` bytes long, starting with a CRC8 (polynomial 0x07) of
the remaining bytes, followed by a header byte with the version (1) in bits 3:0
and log2 of the size in bits 7:4, e.g. `0x61` for 0x40 bytes or `0x41` for 0x10.
The 32-bit little-endian flags at offset 8 hold the try count (bits 1:0), try-B
(bit 2), recovery (bit 3), the result of the last try (bits 5:4: unknown,
trying, ok, bad) and the copy which was picked (bits 7:6: A, B, recovery).

Each slot has its own slot info, a small devicetree at `slot-info-offset` which
describes the firmware in that slot. When fwupd writes an update to B, it writes
this too, before asking for B to be tried, so the slot info moves to A along
with the firmware. Its root node has these properties, and more may be added
later, so readers should ignore any they do not know:

* `compatible` - `fwupd,vbe-slot-info`, so that a reader can tell slot info from
  any other devicetree which happens to be there
* `version` - version of the firmware, as a string
* `size` - number of bytes of firmware at the start of the slot
* `crc32` - CRC32 (as used by zlib) of those bytes
* `sha256` - SHA256 of those bytes

A slot whose slot info is not a valid devicetree with this compatible string,
e.g. one written by an older tool, has no known version.

#### Checking the state

While an update is waiting to be tried, or is being tried, the device shows a
problem in `fwupdmgr get-devices` and fwupd does not install another update over
it:

```text
└─abrec:
      Summary:            A/B/recovery firmware
      Current version:    2026.10.3
      Problems:           • Device is waiting for the update to be applied
```

This also happens if recovery is requested before B is tried. The update then
stays pending until recovery is turned off and B has had its tries.

After the reboot which tries an update, `fwupdmgr get-history` gives the result:

* success, once B has reached the OS and been copied to A
* a failure if B did not reach the OS, with the reason: `new firmware failed to
  boot, so A is still in use`, or `new firmware failed to boot and A could not
  be loaded, so recovery is in use`
* still pending, if recovery was requested before B was tried
* a failure if B reached the OS but could not be copied to A, e.g. because the
  storage cannot be written; the state still asks for B, so each boot tries B
  and fwupd tries the copy again, but once B runs out of tries the firmware goes
  back to A, which may be partly written by then, and from there to recovery

The firmware reports what it booted in the devicetree, under
`/proc/device-tree/chosen/fwupd/firmware0`: `booted-slot` says which copy was
booted and `cur-version` gives its version. The full state can be read from the
storage at `area-start` plus `state-offset`, and decoded as above. For example,
with the state at 0x7f8000 on `/dev/mmcblk1`:

```shell
sudo dd if=/dev/mmcblk1 bs=512 skip=$((0x7f8000 / 512)) count=1 | od -A x -t x1 | head -1
```

In U-Boot, `vbe info` shows the state, `vbe state` shows which phases used VBE
and `vbe recovery` shows or changes the recovery request.

## Finding out more

You can use the U-Boot mailing list or `u-boot` IRC on `libera.chat` to ask
questions specific to VBE and firmware.

## Sending patches

Use the [fwupd developer site](https://fwupd.org/lvfs/docs/developers) to find
information about downloading the code, submitting bugs/feature requests and
sending patches.

## Vendor ID Security

This does not update USB devices and thus requires no vendor ID set.

## External Interface Access

This plugin requires access to system firmware, e.g. via a file or an eMMC
device.

## Documentation

The following documents may help in understanding VBE:

* [VBE](https://docs.google.com/document/d/e/2PACX-1vQjXLPWMIyVktaTMf8edHZYDrEvMYD_iNzIj1FgPmKF37fpglAC47Tt5cvPBC5fvTdoK-GA5Zv1wifo/pub)
* [VBE Bootflows](https://docs.google.com/document/d/e/2PACX-1vR0OzhuyRJQ8kdeOibS3xB1rVFy3J4M_QKTM5-3vPIBNcdvR0W8EXu9ymG-yWfqthzWoM4JUNhqwydN/pub)
* [VBE Firmware update](https://docs.google.com/document/d/e/2PACX-1vTnlIL17vVbl6TVoTHWYMED0bme7oHHNk-g5VGxblbPiKIdGDALE1HKId8Go5f0g1eziLsv4h9bocbk/pub)
* [FIT](https://fitspec.osfw.foundation/)
* [U-Boot Standard boot](https://u-boot.readthedocs.io/en/latest/develop/bootstd.html)

## Useful information

For development you may find the following useful. There is an example for
each method, in `plugins/vbe/example/simple` and `plugins/vbe/example/abrec`.

### vbe-simple example

To install the example devicetree:

```bash
    dtc /path/to/fwupd/plugins/vbe/example/simple/test.dts -o \
       /var/lib/fwupd/system.dtb
```

To build the example:

```bash
   sudo apt install appstream-util u-boot-tools
   cd /path/to/fwupd/plugins/vbe/example/simple
   dd if=/dev/zero of=update.bin bs=1M count=1
   ./build.sh
```

The update provides the GUID of the `VBE\COMPATIBLE_pine64-rockpro64-v2.1`
instance ID, so it only applies to that board. Note that characters such as `,`
in the compatible string become `-` in the instance ID. Use
`fwupdmgr get-devices --show-all` to see the GUID for each instance ID.

To install the cab on your development computer:

```bash
   # Set up the test file
   dd if=/dev/zero of=/tmp/testfw bs=1M count=3

   sudo build/src/fwupdtool install plugins/vbe/example/simple/Vbe-Board-1.2.4.cab \
      e2363f2d-d877-5d0a-8fb6-725bed478132
```

To dump out the firmware:

```bash
   sudo rm fw.bin; sudo build/src/fwupdtool firmware-dump fw.bin \
      e2363f2d-d877-5d0a-8fb6-725bed478132
```

### vbe-abrec example

To set up the storage and devicetree, then build and install the example:

```bash
   cd /path/to/fwupd/plugins/vbe/example/abrec
   dd if=/dev/zero of=/tmp/testfw bs=1M count=4
   sudo dtc test.dts -o /var/lib/fwupd/system.dtb
   dd if=/dev/zero of=update.bin bs=64K count=1
   ./build.sh

   cd /path/to/fwupd
   sudo build/src/fwupdtool install plugins/vbe/example/abrec/Vbe-Abrec-1.2.4.cab \
      e2363f2d-d877-5d0a-8fb6-725bed478132
```

The update is now in B, waiting to be tried, so `fwupdtool get-devices` shows
that the device is waiting for the update to be applied. On a board, the
firmware tries B on the next boot. Here, `try-b.py` updates the state as the
firmware does, and the devicetree then says that B was booted:

```bash
   cd /path/to/fwupd/plugins/vbe/example/abrec
   ./try-b.py
   sed 's/booted-slot = "a"/booted-slot = "b"/' test.dts | \
      sudo dtc -o /var/lib/fwupd/system.dtb
   cd /path/to/fwupd
   sudo build/src/fwupdtool get-devices
```

fwupd sees that the OS started from B, so it copies B to A and the device shows
version 1.2.4.

## Version Considerations

This plugin has been available since fwupd version `1.8.2`.
The `VBE\COMPATIBLE_xxx` instance ID for each board has been available since
fwupd version `2.2.1`, as has the `vbe-abrec` method.
