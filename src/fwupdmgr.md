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

OPTIONS
=======

The fwupdmgr command takes various options depending on the action.
Run **fwupdmgr \-\-help** for the full list.

EXIT STATUS
===========

Commands that successfully execute will return "0", with generic failure as "1".

There are also several other exit codes used:
A return code of "2" is used for commands that have no actions but were successfully executed,
and "3" is used when a resource was not found.

BUGS
====

See GitHub Issues: <https://github.com/fwupd/fwupd/issues>

SEE ALSO
========

fwupdtool(1)
fwupd.conf(5)
