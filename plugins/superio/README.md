SuperIO
=======

This plugin enumerates the various ITE85* SuperIO embedded controller ICs found
in many laptops. Vendors wanting to expose the SuperIO functionality will need
to add a HwId quirk entry to `superio.quirk`.

See https://en.wikipedia.org/wiki/Super_I/O for more details about SuperIO
and what the EC actually does.

Eventually we could support flashing the EC using this plugin, but not until we
have a way to recover a failed flash. The pragmatic decision is probably to use
the vendor-suplied UEFI capsule binary, as the ITE85* datasheets are seemingly
not available without signing an NDA with ITE.

Other useful links:

* https://raw.githubusercontent.com/system76/ecflash/master/ec.py
* https://github.com/system76/firmware-update/tree/master/src
* https://github.com/coreboot/coreboot/blob/master/util/superiotool/superiotool.h
* https://github.com/flashrom/flashrom/blob/master/it85spi.c
* http://wiki.laptop.org/go/Ec_specification
