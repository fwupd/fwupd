% dfu-tool(1) {{PACKAGE_VERSION}} | dfu-tool man page

NAME
====

**dfu-tool** — write firmware to DFU devices

SYNOPSIS
========

| **dfu-tool** [CMD]

DESCRIPTION
===========

This manual page documents briefly the **dfu-tool** command.

**dfu-tool** allows a user to write various kinds of
firmware onto devices supporting the USB Device Firmware Upgrade protocol.
This tool can be used to switch the device from the normal runtime mode
to DFU mode which allows the user to read and write firmware.
Either the whole device can be written in one operation, or individual
targets can be specified with the alternative name or number.

All synchronous actions can be safely cancelled and on failure will return
errors with both a type and a full textual description.
libdfu supports DFU 1.0, DFU 1.1 and the ST DfuSe vendor extension, and
handles many device quirks necessary for the real-world implementations
of DFU.

OPTIONS
=======

The dfu-tool command takes various options depending on the action.
Run **dfu-tool --help** for the full list.

BUGS
====

See GitHub Issues: <https://github.com/fwupd/fwupd/issues>

SEE ALSO
========

fwupdtool(1), fwupdmgr(1)
