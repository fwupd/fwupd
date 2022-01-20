# fwupd

[![Build Status](https://github.com/fwupd/fwupd/actions/workflows/main.yml/badge.svg)](https://github.com/fwupd/fwupd/actions/workflows/main.yml)
[![Coverity Scan Build Status](https://scan.coverity.com/projects/10744/badge.svg)](https://scan.coverity.com/projects/10744)
[![Fuzzing Status](https://oss-fuzz-build-logs.storage.googleapis.com/badges/fwupd.svg)](https://bugs.chromium.org/p/oss-fuzz/issues/list?sort=-opened&can=1&q=proj:fwupd)
[![CircleCI](https://circleci.com/gh/fwupd/fwupd/tree/main.svg?style=svg)](https://circleci.com/gh/fwupd/fwupd/tree/main)

This project aims to make updating firmware on Linux automatic, safe and reliable.

Additional information is available [at the website](https://fwupd.org/).

## Compiling

The most up to date compilation instructions are available in the [Wiki](https://github.com/fwupd/fwupd/wiki/Compilation).

**NOTE:** In most cases end users should never compile fwupd from scratch; it's a
complicated project with dozens of dependencies (and as many configuration options)
and there's just too many things that can go wrong.

Users should just have fwupd installed and updated by their distro, managed and
tested by the package maintainer.
The distribution will have also done some testing with how fwupd interacts with
other software on your system, for instance using GNOME Software.

Installing fwupd using [Snap](https://github.com/fwupd/fwupd/wiki/fwupd-snap)
or using [Flatpak](https://github.com/fwupd/fwupd/wiki/fwupd-flatpak) might be
useful to update a specific device on the command line that needs a bleeding
edge fwupd version, but it should not be considered as a replacement to the
distro-provided system version.

## LVFS

This project is configured by default to download firmware from the [Linux Vendor
Firmware Service (LVFS)](https://fwupd.org/).

This service is available to all OEMs and firmware creators who would like to make
their firmware available to Linux users.

You can find more information about the technical details of creating a firmware
capsule in the hardware vendors section of the [fwupd website](https://fwupd.org).

## Basic usage flow (command line)

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

## Reporting status

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

## Enterprise use

The flow of updates can be controlled in the enterprise using the
"approved updates" feature. This allows the domain administrator to filter
the possible updates from a central server (e.g. the LVFS, or a mirror)
to only firmware that have been tested specifically in your organization.

The list of approved updates can be enabled by adding `ApprovalRequired=true`
to the remote configuration file, e.g. `lvfs.conf`. Once enabled, the
list of approved updates can be set in `daemon.conf` using a comma delimited list.

For example:

    ApprovedFirmware=foo,bar

Where `foo,bar` refers to the container checksums that would correspond
to two updates in the metadata file.

Additionally, the list of approved firmware can be supplemented using
`fwupdmgr set-approved-firmware baz` or using the D-Bus interface.

## Local metadata

Local metadata can be saved in `/var/lib/fwupd/local.d` or `/usr/share/fwupd/local.d`
which are scanned at daemon startup. This can be used to add site-specific BKC
tags to existing metadata stores. For instance:

    <?xml version='1.0' encoding='utf-8'?>
    <components origin="mycompanyname">
      <component merge="append">
        <provides>
          <firmware>3ef35d3b-ceeb-5e27-8c0a-ac25f90367ab</firmware>
          <firmware>2ef35d3b-ceeb-5e27-8c0a-ac25f90367ac</firmware>
          <firmware>1ef35d3b-ceeb-5e27-8c0a-ac25f90367ad</firmware>
        </provides>
        <releases>
          <release version="225.53.1649"/>
          <release version="224.48.1605"/>
        </releases>
        <tags>
          <tag>mycompanyname-2022q1</tag>
        </tags>
      </component>
    </components>

This then appears when getting the releases for that specific GUID:

    fwupdmgr get-releases --json 3ef35d3b-ceeb-5e27-8c0a-ac25f90367ab
    {
      "Releases" : [
        {
          ...
          "Version" : "225.53.1649",
          "Tags" : [
            "mycompanyname-2022q1"
          ],
          ...
        },
        {
          ...
          "Version" : "224.48.1605",
          "Tags" : [
            "mycompanyname-2022q1"
          ],
          ...
        },
        {
          ...
          "Version" : "224.45.1389",
          ...
        }
      ]
    }

## Other frontends

1. [GNOME Software](https://wiki.gnome.org/Apps/Software) is the graphical
 frontend available. When compiled with firmware support, it will check for
 updates periodically and automatically download firmware in the background.
 After the firmware has been downloaded a popup will be displayed in GNOME
 Software to perform the update.

2. [KDE Discover](https://userbase.kde.org/Discover) is the software center,
 generally bundled with KDE Plasma. With the release of
 [KDE Plasma 5.14](https://www.kde.org/announcements/plasma-5.14.0.php),
 a new fwupd backend has been implemented in KDE Discover for firmware updates.
 These firmware updates are shown with other system updates.

3. [Wyse Management Suite](https://www.dell.com/en-us/work/shop/wyse-endpoints-and-software/wyse-management-suite/spd/wyse-wms)
 A software suite available on Dell IoT gateways and Wyse thin clients with built-in fwupd support.
 The remote administration interface can be used to download and deploy firmware
 updates.

## Fuzzing

There are several automated fuzzing tests in fwupd. These take some time to run:

    CC=hfuzz-clang meson --default-library=static \
        -Dudevdir=/tmp -Dsystemd_root_prefix=/tmp \
        -Dplugin_redfish=false -Dcurl=false \
        -Dintrospection=false ../
    ninja install
    ninja fuzz-firmware
    ninja fuzz-tpm-eventlog
