% fwupdmgr(1) {{PACKAGE_VERSION}} | fwupdmgr man page

NAME
====

**fwupdmgr** — firmware update manager client utility

SYNOPSIS
========

| **fwupdmgr** [CMD]

DESCRIPTION
===========

fwupdmgr is a command line fwupd client intended to be used interactively.
The terminal output between versions of fwupd is not guaranteed to be stable, but if you plan on
parsing the results then adding **\-\-json** might be just what you need.

There are also graphical tools to firmware available for GNOME and KDE.
These applications may be more useful to many users compared to using the command line.

* **GNOME Software**: <https://wiki.gnome.org/Apps/Software>

* **GNOME Firmware**: <https://gitlab.gnome.org/World/gnome-firmware>

* **KDE Discover**: <https://userbase.kde.org/Discover>

On most systems fwupd is configured to download metadata from the Linux Vendor Firmware Service
`https://fwupd.org/` and more information about the LVFS is available here: `https://lvfs.readthedocs.io/`

Most users who want to just update all devices to the latest versions can do `fwupdmgr refresh` and then `fwupdmgr update`.
At this point the system will asking for confirmation, update some devices, and may then reboot to deploy other updates offline.

OPTIONS
=======

The fwupdmgr command takes various options depending on the action.
Run **fwupdmgr \-\-help** for the full list.

EXIT STATUS
===========

Commands that successfully execute will return "0", but commands that have no
actions but successfully execute will return "2".

BUGS
====

See GitHub Issues: <https://github.com/fwupd/fwupd/issues>

SEE ALSO
========

fwupdtool(1)
fwupd.conf(5)
