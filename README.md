# fwupd

[![Build Status](https://github.com/fwupd/fwupd/actions/workflows/ci.yml/badge.svg)](https://github.com/fwupd/fwupd/actions/workflows/ci.yml)
[![CodeQL](https://github.com/fwupd/fwupd/actions/workflows/codeql-analysis.yml/badge.svg)](https://github.com/fwupd/fwupd/actions/workflows/codeql-analysis.yml)
[![Coverity Scan Build Status](https://scan.coverity.com/projects/10744/badge.svg)](https://scan.coverity.com/projects/10744)
[![Fuzzing Status](https://oss-fuzz-build-logs.storage.googleapis.com/badges/fwupd.svg)](https://bugs.chromium.org/p/oss-fuzz/issues/list?sort=-opened&can=1&q=proj:fwupd)
[![CircleCI](https://circleci.com/gh/fwupd/fwupd/tree/main.svg?style=svg)](https://circleci.com/gh/fwupd/fwupd/tree/main)
[![Codecov Coverage Status](https://codecov.io/gh/fwupd/fwupd/graph/badge.svg?token=vykt2ROfu9)](https://codecov.io/gh/fwupd/fwupd)
[![OpenSSF Scorecard](https://api.securityscorecards.dev/projects/github.com/fwupd/fwupd/badge)](https://securityscorecards.dev/viewer/?uri=github.com/fwupd/fwupd)
[![OpenSSF Best Practices](https://www.bestpractices.dev/projects/8751/badge)](https://www.bestpractices.dev/projects/8751)

This project aims to make updating firmware on Linux automatic, safe, and reliable.

Additional information is available [at the website](https://fwupd.org/).

## Where to get help?

- GitHub issues & discussions in [this repository](https://github.com/fwupd/fwupd)
- Libera IRC channel `#fwupd`.
  You can join through Libera's [IRC](https://libera.chat/)
  or via the IRC bridge on [Matrix](https://matrix.to/#/#fwupd:libera.chat).

## Compiling

See [Building and Debugging](docs/building.md) for how to build the fwupd development environment.

**NOTE:** In most cases, end users should not compile fwupd from scratch; it's a
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

### Using Tartan

[Tartan](https://gitlab.freedesktop.org/tartan/tartan/-/wikis/home) is a LLVM static
analysis plugin built to analyze GLib code. It can be installed and then run using:

    mkdir build-tartan
    CC=clang-17 meson ../
    SCANBUILD=../contrib/tartan.sh ninja scan-build

## LVFS

This project is configured by default to download firmware from the [Linux Vendor
Firmware Service (LVFS)](https://fwupd.org/).

This service is available to all OEMs and firmware creators who would like to make
their firmware available to Linux users.

You can find more information about the technical details of creating a firmware
capsule in the hardware vendors section of the [fwupd website](https://fwupd.org).

## Basic usage flow (command line)

If you have a device with firmware supported by fwupd, this is how you can check
for updates and apply them using fwupd's command line tools.

`# fwupdmgr get-devices`

This will display all devices detected by fwupd.

`# fwupdmgr refresh`

This will download the latest metadata from LVFS.

`# fwupdmgr get-updates`

If updates are available for any devices on the system, they'll be displayed.

`# fwupdmgr update`

This will download and apply all updates for your system.

- Updates that can be applied live will be done immediately.
- Updates that run at bootup will be staged for the next reboot.

You can find more information about the update workflow in the end
users section of the [fwupd website](https://fwupd.org).

## Passim

If the [Passim](https://github.com/hughsie/passim/blob/main/README.md) project is also installed
and enabled, fwupd will re-publish the downloaded metadata file to be served on `0.0.0.0:27500`
by default.

Other clients on the same network can make use of this via mDNS/LLMNR to reduce network bandwidth
to configured remotes.

To disable this functionality either set `P2pPolicy=none` in `/etc/fwupd/daemon.conf`, uninstall
the passim package or use `systemctl mask passim.service` on the terminal.

## Reporting status

fwupd will encourage users to report both successful and failed updates back
to LVFS.  This is an optional feature, but encouraged as it provides valuable
feedback to LVFS administrators and OEM developers regarding firmware update
process efficacy.

The privacy policy regarding this data can be viewed on the [lvfs readthedocs site](https://lvfs.readthedocs.io/en/latest/privacy.html).

To report the status of an update, run:

`# fwupdmgr report-history`

Only updates that were distributed from the LVFS will be reported to the LVFS.

## Enterprise use

The flow of updates can be controlled in the enterprise using the
"approved updates" feature. This allows the domain administrator to filter
the possible updates from a central server (e.g. the LVFS, or a mirror)
to only firmware that have been tested specifically in your organization.

The list of approved updates can be enabled by adding `ApprovalRequired=true`
to the remote configuration file, e.g. `lvfs.conf`. Once enabled, the
list of approved updates can be set in `fwupd.conf` using a comma-delimited list.

For example:

    ApprovedFirmware=foo,bar

Where `foo,bar` refers to the container checksums that would correspond
to two updates in the metadata file.

Additionally, the list of approved firmware can be supplemented using
`fwupdmgr set-approved-firmware baz` or using the D-Bus interface.

## Other frontends

fwupdmgr is a command line client, but various additional graphical frontends are enumerated in the [fwupdmgr man page](https://fwupd.github.io/libfwupdplugin/fwupdmgr.html#description).

## SAST Tools

- [Coverity](https://scan.coverity.com/) - static analyzer for Java, C/C++, C#, JavaScript, Ruby, and Python code.
- [PVS-Studio](https://pvs-studio.com/en/pvs-studio/?utm_source=website&utm_medium=github&utm_campaign=open_source) - static analyzer for C, C++, C#, and Java code.
