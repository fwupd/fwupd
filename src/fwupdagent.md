% fwupdagent(1) @PACKAGE_VERSION@ | fwupdagent man page

NAME
====

**fwupdagent** — firmware updating agent

SYNOPSIS
========

| **fwupdagent** [CMD]

DESCRIPTION
===========

fwupdagent used to be a command line fwupd client intended to be used by scripts.
You should now use the 100% compatible **fwupdmgr \-\-json** command instead.
The output is JSON and guaranteed to be stable.

EXIT STATUS
===========

Commands that successfully execute will return "0".

BUGS
====

See GitHub Issues: <https://github.com/fwupd/fwupd/issues>

SEE ALSO
========

fwupdmgr(1), fwupdtool(1)
