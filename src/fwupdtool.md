% fwupdtool(1) {{PACKAGE_VERSION}} | standalone firmware update utility man page

NAME
====

**fwupdtool** — standalone firmware update utility

SYNOPSIS
========

| **fwupdtool** [CMD]

DESCRIPTION
===========

This tool allows an administrator to use fwupd plugins directly without using the daemon process,
which may be faster or easier to use when creating or debugging specific plugins.
For most end-users, **fwupdmgr** is a more suitable program to use in almost all cases.

Additionally **fwupdtool** can be used to convert firmware from various different formats,
or to modify the images contained inside the container firmware file.
For example, you can convert DFU or Intel HEX firmware into the vendor-specific format.

OPTIONS
=======

The fwupdtool command takes various options depending on the action.
Run **fwupdtool \-\-help** for the full list.
Note that some runtimes failures can be ignored using **\-\-force**.

EXIT STATUS
===========

Commands that successfully execute will return "0", but commands that have no
actions but successfully execute will return "2".

BUGS
====

See GitHub Issues: <https://github.com/fwupd/fwupd/issues>

SEE ALSO
========

fwupdmgr(1)
fwupd.conf(5)
