TPM Event Log Support
=====================

Introduction
------------

The TPM Event Log records which events are registered for the PCR0 hash, which
may help in explaining why PCR0 values are differing for some firmware.

The device exposed is not upgradable in any way and is just for debugging.
The created device will be a child device of the system TPM device, which may
or may not be upgradable.

Vendor ID Security
------------------

The device is not upgradable and thus requires no vendor ID set.
