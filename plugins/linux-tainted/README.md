Linux Kernel Tainted
====================

Introduction
------------

This plugin checks if the currently running kernel is tainted. The result will
be stored in an security attribute for HSI.

External interface access
-------------------------
This plugin requires read access to `/sys/kernel/tainted`.
