fwupd
=====
[![Build Status](https://travis-ci.org/hughsie/fwupd.png?branch=master)](https://travis-ci.org/hughsie/fwupd)
[![Coverity Scan Build Status](https://scan.coverity.com/projects/10744/badge.svg)](https://scan.coverity.com/projects/10744)

This project aims to make updating firmware on Linux automatic, safe and reliable.

Additional information is available at the website: https://fwupd.org

## Compiling

The most up to date compilation instructions are available in the [Wiki](https://github.com/hughsie/fwupd/wiki/Compilation)

LVFS
----
This project is configured by default to download firmware from the [Linux Vendor
Firmware Service (LVFS)](https://fwupd.org/).

This service is available to all OEMs and firmware creators who would like to make
their firmware available to Linux users.

You can find more information about the technical details of creating a firmware
capsule in the hardware vendors section of the [fwupd website](https://fwupd.org).

Basic usage flow (command line)
------------------------------

If you have a device with firmware supported by fwupd, this is how you will check
for updates and apply them using fwupd's command line tools.

`# fwupdmgr get-devices`

This will display all devices detected by fwupd.

`# fwupdmgr refresh`

This will download the latest metadata from LVFS.

`# fwupdmgr get-updates`

If updates are available for any devices on the system, they'll be displayed.

`# fwupdmgr update`

This will download and apply all updates for your system.

* Updates that can be applied live will be done immediately.
* Updates that run at bootup will be staged for the next reboot.

You can find more information about the update workflow in the end
users section of the [fwupd website](https://fwupd.org).

Reporting status
---------------

fwupd will encourage users to report both successful and failed updates back
to LVFS.  This is an optional feature, but encouraged as it provides valuable
feedback to LVFS administrators and OEM developers regarding firmware update
process efficacy.

The privacy policy regarding this data can be viewed on the [fwupd website](https://fwupd.org/privacy).

To report the status of an update run:

`# fwupdmgr report-history`

To clear the local history of updates:

`# fwupdmgr clear-history`

 Only updates that were distributed from the LVFS will be reported to the LVFS.

Other frontends
-------------------

Currently [GNOME Software](https://wiki.gnome.org/Apps/Software) is the only graphical
frontend available.  When compiled with firmware support, it will check for updates
periodically and automatically download firmware in the background.

After the firmware has been downloaded a popup will be displayed in Gnome Software
to perform the update.

On Dell IoT gateways, [Wyse Cloud Client Manager (CCM)](http://www.dell.com/us/business/p/wyse-cloud-client-manager/pd)
has been built with fwupd support.
The remote administration interface can be used to download and deploy
firmware updates.
