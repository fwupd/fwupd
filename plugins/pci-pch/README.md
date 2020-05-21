PCI PCH
=======

Introduction
------------

This plugin checks if the PCH has Direct Connect Interface (DCI) enabled.
DCI allows debugging of Intel processors using the USB3 port. DCI should always
be disabled on production hardware as it allows the attacker to disable other
firmware protection methods.

The result will be stored in a security attribute for HSI.
