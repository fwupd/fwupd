fwupd
=====

This project aims to make updating firmware on Linux automatic, safe and reliable.

Additional information is available at the website: http://www.fwupd.org

Adding a new plugin
-------------------

An extensible architecture allows for providing new plugin types (for reading
and writing different firmware) as well as ways quirk their behavior.

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
