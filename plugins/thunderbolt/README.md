Thunderbolt™ Support
====================

Introduction
------------

Thunderbolt™ is the brand name of a hardware interface developed by Intel that
allows the connection of external peripherals to a computer.
Versions 1 and 2 use the same connector as Mini DisplayPort (MDP), whereas
version 3 uses USB Type-C.

Runtime Power Management
------------------------

Thunderbolt controllers are slightly unusual in that they power down completely
when no thunderbolt devices are detected. This poses a problem for fwupd as
it can't coldplug devices to see if there are firmware updates available, and
also can't ensure the controller stays awake during a firmware upgrade.

On Dell hardware the `Thunderbolt::CanForcePower` metadata value is set as the
system can force the thunderbolt controller on during coldplug or during the
firmware update process. This is typically done calling a SMI or ACPI method
which asserts the GPIO for the duration of the request.

On non-Dell hardware you will have to insert a Thunderbolt device (e.g. a dock)
into the laptop to be able to update the controller itself.

Safe Mode
---------

Thunderbolt hardware is also slightly unusual in that it goes into "safe mode"
whenever it encounters a critical firmware error, for instance if an update
failed to be completed. In this safe mode you cannot query the controller vendor
or model and therefore the thunderbolt plugin cannot add the correct GUID used
to match it to the correct firmware.

In this case the metadata value `Thunderbolt::IsSafeMode` is set which would
allow a different plugin to add the correct GUID based on some out-of-band
device discovery. At the moment this only happens on Dell hardware.
