Test Support
============

Introduction
------------

This plugin is used when running the self tests in the fwupd project.

GUID Generation
---------------

The devices created by this plugin use hardcoded GUIDs that do not correspond
to any kind of DeviceInstanceId values.

In other cases devices use the standard BLE DeviceInstanceId values, e.g.

 * `USB\VID_2DC8&PID_AB11`

Vendor ID Security
------------------

The fake device is only for local testing and thus requires no vendor ID set.

External interface access
-------------------------
This plugin requires no extra access.
