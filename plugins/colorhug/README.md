ColorHug Support
================

Introduction
------------

The ColorHug is an affordable open source display colorimeter built by
Hughski Limited. The USB device allows you to calibrate your screen for
accurate color matching.

ColorHug versions 1 and 2 support a custom HID-based flashing protocol, but
version 3 (ColorHug+) has now switched to DFU.

Build Requirements
------------------

For colorhug support you need to install colord 1.2.12 or later.
* source:		https://github.com/hughsie/colord
* rpms:			http://people.freedesktop.org/~hughsient/fedora/
* debs (Debian):	https://tracker.debian.org/pkg/fwupd
* debs (Ubuntu):	https://launchpad.net/ubuntu/+source/fwupd

If you don't want or need this functionality you can use the
`--disable-colorhug` option.
