# Flashrom

## Introduction

This plugin uses `libflashrom` to update the system firmware.  It can be used
to update BIOS or ME regions of the flash.  Device for ME region is created
only if "Intel SPI" plugin indicates that such a region exists, which makes
"Intel SPI" a dependency of this plugin for doing ME updates.

## Firmware Format

The daemon will decompress the cabinet archive and extract a firmware blob in
an unspecified binary file format, which is typically the raw input for an
EEPROM programmer.

This plugin supports the following protocol ID:

* org.flashrom

## Coreboot Version String

The coreboot version string can have an optional prefix (see below).
After the optional prefix the *major*, *minor* string follows and finally
the *build string*, containing the exact commit and repository state, follows.

For example `4.10-989-gc8a4e4b9c5-dirty`

### Exception on Lenovo devices

The thinkpad_acpi kernel module requires a specific pattern in the DMI version
string. To satisfy those requirements coreboot adds the CBETxxxx prefix to the
DMI version string on all Lenovo devices.

For example `CBET4000 4.10-989-gc8a4e4b9c5-dirty`

The coreboot DMI version string always starts with `CBET`.

## GUID Generation

Internal device uses hardware ID values which are derived from SMBIOS.

* HardwareID-3
* HardwareID-4
* HardwareID-5
* HardwareID-6
* HardwareID-10

They should match the values provided by `fwupdtool hwids` or the
`ComputerHardwareIds.exe` Windows utility.

One more GUID has the following form:

* `FLASHROM\VENDOR_{manufacturer}&PRODUCT_{product}&REGION_{ifd_region_name}`

Its purpose is to target specific regions of the flash as defined by IFD (Intel
SPI Flash Descriptor), examples:

* `FLASHROM\VENDOR_Notebook&PRODUCT_NS50MU&REGION_BIOS`
* `FLASHROM\VENDOR_Notebook&PRODUCT_NS50MU&REGION_ME`

## Update Behavior

The firmware is deployed to the SPI chip when the machine is in normal runtime
mode, but it is only used when the device is rebooted.

## Vendor ID Security

The vendor ID is set from the BIOS vendor, for example `DMI:Google`

## External Interface Access

This plugin requires access to all interfaces that `libflashrom` has been compiled for.
This typically is `/sys/bus/spi` but there may be other interfaces as well.
