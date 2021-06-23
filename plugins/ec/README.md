EC
==

This plugin provides flashing support for Embedded Controllers that use
custom protocols.

See https://en.wikipedia.org/wiki/Embedded_controller for more details about
what the EC actually does.

GUID Generation
---------------

These devices use a custom GUID generated using the EC chipset name:

 * `EC-$(chipset)`, for example `EC-IT5570`

Update Behavior
---------------

The firmware is deployed when the device is in normal runtime mode, but it is
only activated on machine reboot. The firmware write is normally scheduled to be
done very early in the boot process to minimize the chance the EC chip locking
up if the user is actually using the keyboard controller.

Vendor ID Security
------------------

The vendor ID is set from the baseboard vendor, for example `DMI:Star Labs`.

Configuration
-------------

The following parameters can be specified in `/etc/fwupd/ec.conf`.

**AutoloadAction** parameter can be set to `none`, `disable`, `seton` or
`setoff`. `none` is the default.

**DoNotRequireAC** parameter is boolean (`true` or `false`).  `false` is the
default.

Example configuration file:

	[ec]
	AutoloadAction=disable
	DoNotRequireAC=false

Quirk use
---------

This plugin uses the following plugin-specific quirks:

| Quirk           | Description                                       | Minimum fwupd version |
|-----------------|---------------------------------------------------|-----------------------|
| `EcControlPort` | Control (status/command) port number, e.g. `0x66` | 1.6.2                 |
| `EcDataPort`    | Data port number, e.g. `0x62`                     | 1.6.2                 |


External interface access
-------------------------

This plugin requires access to raw system memory via `inb`/`outb`.
