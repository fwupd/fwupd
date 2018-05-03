Wacom HID Support
=================

Introduction
------------

Wacom provides interactive pen displays, pen tablets, and styluses to equip and
inspire everyone make the world a more creative place.

From 2016 Wacom has been using a HID-based proprietary flashing algorithm which
has been documented by support team at Wacom and provided under NDA under the
understanding it would be used to build a plugin under a LGPLv2+ licence.

Wacom devices are actually composite devices, with the main ARM CPU being
programmed using a more complicated erase, write, verify algorithm based
on a historical update protocol. The "sub-module" devices use a newer protocol,
again based on HID, but are handled differently depending on thier type.
