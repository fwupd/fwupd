fwupd
=====

This project aims to make updating firmware on Linux automatic, safe and reliable.

Additional information is available at the website: http://www.fwupd.org

DFU Support
-----------

DFU support is supported directly by this project with the embedded libdfu
library.

ColorHug Support
----------------

For colorhug support you need to install colord 1.2.12 or later.
* source:		https://github.com/hughsie/colord
* rpms:			http://people.freedesktop.org/~hughsient/fedora/
* debs (Debian):	https://tracker.debian.org/pkg/fwupd
* debs (Ubuntu):	https://launchpad.net/ubuntu/+source/fwupd

If you don't want or need this functionality you can use the
`--disable-colorhug` option.

UEFI Support
------------

For UEFI capsule support, you need to install fwupdate 0.5 or later.
* source:		https://github.com/rhinstaller/fwupdate
* rpms:			https://pjones.fedorapeople.org/fwupdate/
* debs (Debian):	https://tracker.debian.org/pkg/fwupdate
* debs (Ubuntu):	https://launchpad.net/ubuntu/+source/fwupdate

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

Dell Support
----------------

This allows installing Dell capsules that are not part of the ESRT table.

For Dell support you will need libsmbios_c version 2.3.0 or later and
efivar.
* source:		http://linux.dell.com/cgi-bin/cgit.cgi/libsmbios.git/
* rpms:		https://apps.fedoraproject.org/packages/libsmbios
* debs (Debian):	http://tracker.debian.org/pkg/libsmbios
* debs (Ubuntu):	http://launchpad.net/ubuntu/+source/libsmbios

Raspberry Pi support
--------------------

A provider is available that can flash the boot firmware on the Raspberry Pi.

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

Basic usage flow (command line)
------------------------------

If you have a device with firmware supported by fwupd, this is how you will check
for updates and apply them using fwupd's command line tools.

`fwupdmgr get-devices`

This will display all devices detected by fwupd.

`fwupdmgr refresh`

This will download the latest metadata from LVFS.

`fwupdmgr get-updates`

If updates are available for any devices on the system, they'll be displayed.

`fwupdmgr update`

This will download and apply all updates for your system.

* Updates that can be applied live *(Online updates)* will be done immediately.
* Updates that require a reboot *(Offline updates)* will be staged for the next reboot.

Other frontends
-------------------

Currently [GNOME Software] (https://wiki.gnome.org/Apps/Software) is the only graphical
frontend available.  When compiled with firmware support, it will check for updates
periodically and automatically download firmware in the background.

After the firmware has been downloaded a popup will be displayed in Gnome Software
to perform the update.

On Dell IoT gateways, [Wyse Cloud Client Manager (CCM)] (http://www.dell.com/us/business/p/wyse-cloud-client-manager/pd)
has been built with fwupd support.
The remote administration interface can be used to download and deploy
firmware updates.
