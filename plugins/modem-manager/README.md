ModemManager
============

Introduction
------------

This plugin adds support for devices managed by ModemManager.

GUID Generation
---------------

These device use the ModemManager "Firmware Device IDs" as the GUID, e.g.

 * `USB\VID_413C&PID_81D7&REV_0318&CARRIER_VODAFONE`
 * `USB\VID_413C&PID_81D7&REV_0318`
 * `USB\VID_413C&PID_81D7`
 * `USB\VID_413C`

Vendor ID Security
------------------

The vendor ID is set from the USB vendor, for example `USB:0x413C`

Update method: fastboot
-----------------------

If the device supports the 'fastboot' update method, it must also report which
AT command should be used to trigger the modem reboot into fastboot mode.

Once the device is in fastboot mode, the firmware upgrade process will happen
as defined e.g. in the 'flashfile.xml' file. Every file included in the CAB that
is not listed in the associated 'flashfile.xml' will be totally ignored during
the fastboot upgrade procedure.

Update method: qmi-pdc
----------------------

If the device supports the 'qmi-pdc' update method, the contents of the CAB
file should include files named as 'mcfg.*.mbn' which will be treated as MCFG
configuration files to download into the device using the Persistent Device
Configuration QMI service.

If a device supports both 'fastboot' and 'qmi-pdc' methods, the fastboot
operation will always be run before the QMI operation, so that e.g. the full
partition where the MCFG files are stored can be wiped out before installing
the new ones.

