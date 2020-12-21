UEFI PK Support
===============

Introduction
------------

The platform key (PK) specifies the machine owner, typically the OEM
that created the laptop or desktop.

Several device manufacturers decide to ship the default "AMI Test PK"
platform key instead of a Device Manufacturer specific one. This will
cause an HSI-1 failure.

External interface access
-------------------------

This plugin requires:
* read access to `/sys/firmware/efi/efivars`
