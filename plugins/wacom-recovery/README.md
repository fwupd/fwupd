Wacom recovery support
=================

Introduction
------------

This plugin will read platform data to determine what VID/PID match the Wacom
panel in the system, allowing a corrupted panel to be recovered.

GUID Generation
---------------
GUIDs are stored in fwupd quirk files with keys that match hardware IDs.

Custom flag use:
----------------
This plugin uses the following plugin-specific custom flags:
* `supports-wacom-recovery`: The HWId supports reading GPIO strapping for GUIDs

Quirk use
---------
This plugin uses the following plugin-specific quirks:
| Quirk                        | Description                                                             | Minimum fwupd version |
|------------------------------|-------------------------------------------------------------------------|-----------------------|
|WacomRecoveryGpioChip| The name of the GPIO chip as recognized by the kernel | 1.3.2|
|WacomRecoveryGpioLines| The GPIO lines that represent strapping | 1.3.2|
|WacomRecoveryGpio%d| The GUID that a GPIO line represents| 1.3.2|
