Thunderbolt™ Support
====================

Introduction
------------

Thunderbolt™ is the brand name of a hardware interface developed by Intel that
allows the connection of external peripherals to a computer.
Versions 1 and 2 use the same connector as Mini DisplayPort (MDP), whereas
version 3 uses USB Type-C.

Comments
--------

The design of this plugin is somewhat different to other plugins. In the authors
opinion, the suboptimal design features are as follows:

 * There are no signals from `libtbtfwu` when devices are added or removed.
 * Individual `tbt_fwu_Controller`'s cannot be refcounted and previously
   returned objects are invalid as soon as `tbt_fwu_getControllerList()` is
   called. This means we have to have a 'lookaside' array of info structs which
   we have to invalidate manually.
 * The ID, VendorID, ModelID and NVMVersion are implemented by calling methods
   rather than reading properties off an object. This means reading properties
   are not cached and can fail, which is also why the lookaside array is used.
 * There is a hardcoded 3s delay on hotplug and unplug as the daemon state can
   get out of sync with the UDev list of devices.
 * The `tbt_fwu_Controller_validateFWImage()` does image validation client side
   which is apparently delibertate.
 * The daemon does not keep track of the physical devices, so we have to check
   a list of cached added sysfs paths to be able to do a rescan only when a
   Thunderbolt controller is removed.

Build Requirements
------------------

For Thunderbolt online update support, you need to install libtbtfwu.
* source:		https://github.com/01org/thunderbolt-software-user-space/tree/fwupdate

If you don't want or need this functionality you can use the
`--disable-thunderbolt` option.
