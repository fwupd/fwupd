UEFI dbx Support
================

Introduction
------------

Updating the UEFI revocation database prevents starting EFI binaries with known
security issues, and is typically no longer done from a firmware update due to
the risk of the machine being "bricked" if the bootloader is not updated first.

This plugin also checks if the UEFI dbx contains all the most recent revoked
checksums. The result will be stored in an security attribute for HSI.

Firmware Format
---------------

The daemon will decompress the cabinet archive and extract a firmware blob in
EFI_SIGNATURE_LIST format.

See https://www.uefi.org/sites/default/files/resources/UEFI%20Spec%202_6.pdf
for details.

This plugin supports the following protocol ID:

 * org.uefi.dbx

GUID Generation
---------------

These devices use the GUID constructed of the uppercase SHA256 of the X509
certificates found in the system KEK and optionally the EFI architecture. e.g.

 * `UEFI\CRT_{sha256}`
 * `UEFI\CRT_{sha256}&ARCH_{arch}`

...where `arch` is typically one of `IA32`, `X64`, `ARM` or `AA64`

Update Behavior
---------------

The firmware is deployed when the machine is in normal runtime mode, but it is
only activated when the system is restarted.

Vendor ID Security
------------------

The vendor ID is hardcoded to `UEFI:Microsoft` for all devices.


External interface access
-------------------------
This plugin requires:
* read/write access to `/sys/firmware/efi/efivars`
