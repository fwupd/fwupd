---
title: Device Emulation
---

## Introduction

Using device-tests, fwupd can prevent regressions by updating and downgrading firmware on real
hardware. However, much past a few dozen devices this does not scale, either by time, or because
real devices need plugging in and out. We can unit test the plugin internals, but this does not
actually test devices being attached, removed, and being updated.

By recording the backend devices we can build a "history" in GUsb of what control, interrupt and
bulk transfers were sent to, and received from the device. By dumping these we can "replay" the
update without the physical hardware connected.

There are some problems that make this emulation slightly harder than the naive implementation:

* Devices are sometimes detached into a different "bootloader" device with a new VID:PID
* Devices might be "composite" and actually be multiple logical devices in one physical device
* Devices might not be 100% deterimistic, e.g. queries might be processed out-of-order

For people to generate and consume emulated devices, we do need to make the process easy to
understand, and also easy to use. Some key points that we think are important, is the ability to:

* Dump the device of an unmodified running daemon.
* Filter to multiple or single devices, to avoid storing data for unrelated parts of the system.
* Load an emulated device into an unmodified running daemon.

Because we do not want to modify the daemon, we think it makes sense to *load* and *save* emulation
state over D-Bus. Each phase can be controlled, which makes it easy to view, and edit, the recorded
emulation data.

For instance, calling `fwupdmgr emulate-add` would ask the end user to choose a device to start
*recording* so that subsequent re-plugs are available to save.
As the device state may not be persistent we save the device-should-be-recorded metadata in the
pending database like we would do for a successful firmware update.

To demo this, something like this could be done:

    # connect ColorHug2
    fwupdmgr modify-config AllowEmulation true
    fwupdmgr emulate-add b0a78eb71f4eeea7df8fb114522556ba8ce22074
    # or, using the GUID
    # fwupdmgr emulate-add 2082b5e0-7a64-478a-b1b2-e3404fab6dad
    # remove and re-insert ColorHug2
    fwupdmgr get-devices --filter allow-emulate-save
    fwupdmgr download https://fwupd.org/downloads/170f2c19f17b7819644d3fcc7617621cc3350a04-hughski-colorhug2-2.0.6.cab
    fwupdmgr install e5* --allow-reinstall
    fwupdmgr emulate-save colorhug.zip
    # remove ColorHug2
    fwupdmgr emulate-load colorhug.zip
    fwupdmgr get-devices --filter emulated
    fwupdmgr install e5* --allow-reinstall
    fwupdmgr modify-config AllowEmulation false

## Device Tests

The `emulation-url` string parameter can be specified in the `steps` section of a specific device
test. This causes the front end to load the emulation data before running the specific step.
