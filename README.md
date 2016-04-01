fwupd
=====

This project aims to make updating firmware on Linux automatic, safe and reliable.
Additional information is available at the website: https://www.fwupd.org

DFU Support
-----------

DFU support is supported directly by this project with the embedded libdfu1
library.

ColorHug Support
----------------

For colorhug support you need to install colord 1.2.12.
* source:		https://github.com/hughsie/colord
* rpms:		http://people.freedesktop.org/~hughsient/fedora/
* debs (Debian):	https://tracker.debian.org/pkg/fwupd
* debs (Ubuntu):	https://launchpad.net/ubuntu/+source/fwupd

If you don't want or need this functionality you can use the
`--disable-colorhug` option.

UEFI Support
------------

For UEFI capsule support, you need to install fwupdate 0.5.
* source:		https://github.com/rhinstaller/fwupdate
* rpms:		https://pjones.fedorapeople.org/fwupdate/
* debs (Debian):	https://tracker.debian.org/pkg/fwupdate
* debs (Debian):	https://launchpad.net/ubuntu/+source/fwupdate

If you don't want or need this functionality you can use the 
`--disable-uefi` option.

UEFI Unlock Support
-------------------

On some Dell systems it's possible to turn on and off UEFI capsule
support from within the BIOS.  This functionality can also be adjusted
from within the OS by fwupd. This requires using fwupdate 0.6 or later 
and compiling it with libsmbios support.

When fwupd and fwupdate have been compiled with this support you will
be able to enable UEFI support on the device by using the `unlock` command.

Raspberry Pi support
--------------------

A provider is avaialable that can flash the boot firmware on the Raspberry Pi.

Adding a new provider
---------------------

An extensible architecture allows for providing new Provider types (for reading
and writing different firmware) as well as for plugins that can extend existing
firmware providers to quirk their behavior.

If you have a firmware specification and would like to see support
in this project, please file an issue and share the spec.  Patches are also
welcome.

LVFS
----
This project is configured by default to download firmware from the [Linux Vendor
Firmware Service (LVFS)] (https://secure-lvfs.rhcloud.com/lvfs/).

This service is available to all OEMs and firmware creators who would like to make
their firmware available to Linux users.
