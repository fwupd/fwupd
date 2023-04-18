% fwupdmgr(1) @PACKAGE_VERSION@ | fwupdmgr man page

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
