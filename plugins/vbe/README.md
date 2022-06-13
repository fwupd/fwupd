# Verified Boot for Embedded (VBE)

## Introduction

This plugin is used for boards which use the VBE system. This allows the
platform to be updated from user space.

## Firmware Format

Firmware updates are held within a CAB file, a Windows format which allows
files to be collected and compressed, similar to a tar file. The CAB file can be
created using the `gcab` tool, as shown in the VBE `example` directory.

Inside the CAB file is the firmware itself, in Flat Image Tree (FIT) format.
This is typically called `firmware.fit` and can be created by the `mkimage` tool,
or using device tree tools such as `dtc` and `fdtput`.

The FIT supports multiple configuration, each of which is intended to update a
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
various fields to indicate its type as well as option hashes. The `data`
property contains the actual firmware data.

Multiple images can be including in each configuration, but each must have a
different `store-offset` property, indicating the offset from the start of the
firmware region where this particular image is kept. The default store offset
is zero, which is typically suitable if there is only one image.

It is common to use an 'external' FIT, meaning that the `data` property is
omitted and the data is placed in the same file after the FIT itself. In this
case `data-offset` and `data-size` allow the data to be relocated. The data is
stored started at the next 32-bit aligned file position after the FIT. The
`mkimage` tool allows converting a FIT to an external FIT, with the `-E` flag.

See the `plugins/vbe/example` directory for an example of building a firmware
update.

## Operation

The daemon will decompress the cabinet archive and extract the firmware blob,
then write it to storage. The firmware will be used on the next reboot, so no
changes to firmware happen until there is a reboot.

This plugin supports the following protocol ID:

* `org.vbe`

## Board information

VBE requires the board firmware to provide information about the firmware within
the device tree passed to the Operating System.

For systems without device tree, currently the only option is to install a file
for use by fwupd, typically in `/var/lib/fwupd/vbe/system.dtb`.

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

* `/var/lib/fwupd/vbe/system.dtb` - this file holds the system
  information. It overrides the system device tree if any, meaning that the
  system device tree is ignored if this file is preset. For systems that don't
  support device tree (e.g ACPI systems), this file is needed for VBE to work

## Available VBE methods

Note that all VBE methods must subclass `FuVbeDevice` since it provides access to
the device tree and the VBE directory, among other things.

At present only one method is available:

* `fwupd,vbe-simple` - writes a single copy of the firmware to media

### vbe-simple

With this, only a single copy of the firmware is available so if the write
fails, the board may not boot. This is implemented in the `vbe-simple.c` file and
has the device GUID `ea1b96eb-a430-4033-8708-498b6d98178b` within fwupd.

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
   used by fwupd at present but may allow the bootloader to check the fwupd
   state on boot
* `part-id` - the partition number containing the fwupd state. This is not
   used by fwupd at present but may allow the bootloader to check the fwupd
   state on boot

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
* [FIT](https://github.com/u-boot/u-boot/blob/master/doc/uImage.FIT/source_file_format.txt)
* [U-Boot Standard boot](https://u-boot.readthedocs.io/en/latest/develop/bootstd.html)

## Useful information

For development you may find the following useful.

To set up the vbe directory:

```bash
    mkdir /var/lib/fwupd/vbe
    chmod a+rwx /var/lib/fwupd/vbe
    dtc /path/to/fwupd/plugins/vbe/example/test.dts -o \
       /var/lib/fwupd/vbe/system.dtb
```

To build the example:

```bash
   sudo apt install appstream-util u-boot-tools
   cd /path/to/fwupd/plugins/vbe/example
   dd if=/dev/zero of=update.bin bs=1M count=1
   ./build.sh
```

To install the cab on your development computer:

```bash
   # Set up the test file
   dd if=/dev/zero of=/tmp/testfw bs=1M count=3

   sudo build/src/fwupdtool install plugins/vbe/example/Vbe-Board-1.2.4.cab \
      bb3b05a8-ebef-11ec-be98-d3a15278be95
```

To dump out the firmware:

```bash
   sudo rm fw.bin; sudo build/src/fwupdtool firmware-dump fw.bin \
      bb3b05a8-ebef-11ec-be98-d3a15278be95
```

## To do

Add an update method that actually supports verified boot, including maintaining
update state. The update happens in two passes, the first installing firmware
in the 'B' slot and the second writing it to the 'A' slot, to avoid bricking the
device in the event of a write failure or non-functional firmware.

Figure out how to select the right update for a board out of many that might be
on LVFS. At present the selection mechanism only works within the FIT
configuration. This probably needs to use the `<firmware type="flashed">` piece
of the xml file to specify an identifier for the family of boards supported by
the update.
