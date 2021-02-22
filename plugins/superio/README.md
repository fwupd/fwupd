SuperIO
=======

This plugin enumerates the various ITE85* SuperIO embedded controller ICs found
in many laptops. Vendors wanting to expose the SuperIO functionality will need
to add a HwId quirk entry to `superio.quirk`.

See https://en.wikipedia.org/wiki/Super_I/O for more details about SuperIO
and what the EC actually does.

Other useful links:

* https://raw.githubusercontent.com/system76/ecflash/master/ec.py
* https://github.com/system76/firmware-update/tree/master/src
* https://github.com/coreboot/coreboot/blob/master/util/superiotool/superiotool.h
* https://github.com/flashrom/flashrom/blob/master/it85spi.c
* http://wiki.laptop.org/go/Ec_specification

GUID Generation
---------------

These devices use a custom GUID generated using the SuperIO chipset name:

 * `SuperIO-$(chipset)`, for example `SuperIO-IT8512`

Update Behavior
---------------

The firmware is deployed when the device is in normal runtime mode, but it is
only activated on machine reboot. The firware write is normally scheduled to be
done very early in the boot process to minimize the chance the EC chip locking
up if the user is actually using the kerboard controller.

Vendor ID Security
------------------

The vendor ID is set from the baseboard vendor, for example `DMI:Star Labs`

External interface access
-------------------------
This plugin requires access to raw system memory via `inb`/`outb`.
