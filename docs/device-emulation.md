---
title: Device Emulation
---

## Introduction

Using device-tests, fwupd can prevent regressions by updating and downgrading firmware on real
hardware. However, much past a few dozen devices this does not scale, either by time, or because
real devices need plugging in and out. We can unit test the plugin internals, but this does not
actually test devices being attached, removed, and being updated.

By recording the backend devices we can build a "history" what USB control, interrupt and
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

## Tell the daemon to record a device

The **emulation-tag** feature for the daemon is used to notify the daemon to record emulation
data for a device.
If the device you will be recording from is *hotpluggable* you will re-plug the device.
If the device is *persistent* you will need to restart the daemon to get the setup events.

Here is how a hotplugged device would be recorded from:

    # connect ColorHug2 and use the device ID to tag it
    fwupdmgr emulation-tag b0a78eb71f4eeea7df8fb114522556ba8ce22074
    # or, using the GUID
    # fwupdmgr emulation-tag 2082b5e0-7a64-478a-b1b2-e3404fab6dad
    # remove and re-insert ColorHug2
    fwupdmgr get-devices --filter emulation-tag

For a persistent device:

    # by device ID
    fwupdmgr emulation-tag 02b7ba3cca857b7e1e10a2f7a6767ae5ec76a331
    # or by using a GUID
    fwupdmgr emulation-tag 310f81b5-6fce-501e-acfb-487d10501e78
    # restart the daemon
    fwupdmgr quit
    fwupdmgr get-devices

**NOTE:** *If you are running in the fwupd development environment, you will need to manually
restart the daemon because dbus activation doesn't work in the fwupd development environment.

## Record some data

In both cases the flow to record the data is identical. You will use the device as normally and when you're
done with the update you will record all events into a zip file.

    fwupdmgr download https://fwupd.org/downloads/170f2c19f17b7819644d3fcc7617621cc3350a04-hughski-colorhug2-2.0.6.cab
    fwupdmgr install 17*.cab --allow-reinstall
    fwupdmgr emulation-save colorhug.zip

## Test your data

Now that you have data recorded you can remove your device from the system and then try to load the emulation
data.  You should be able to see the emulated device as well as interact with it.

    fwupdmgr emulation-load colorhug.zip
    fwupdmgr get-devices --filter emulated
    fwupdmgr install 17*.cab --allow-reinstall

## Upload test data to LVFS

Test data can be added to LVFS by visiting the `Assets` tab of the firmware release on LVFS.
There is an upload button, and once uploaded a URL will be available that can be used for device tests.

## Add emulation-only data to the plugin

Rather than simulating a complete firmware update, we can also simulate just enumerating the device.
Normally this provides a decent amount of test coverage for the plugin, but is inferior to providing
emulation of the entire firmware update process from one version to another.

The advantage that the emulation-only test is that it can run as part of a quick *installed-test* on
user systems without any internet access required.
Creating the enumeration-only data also does not need redistributable firmware files uploaded to the
LVFS.

To create emulation-only data, mark the device for emulation using `fwupdmgr emulation-tag` as above,
then replug the device or restart the daemon as required.

Then the enumeration can be extracted into the plugin directory using:

    fwupdmgr emulation-save emulation.zip
    unzip emulation.zip
    mv setup.json tests/plugin-setup.json

The enumeration-only emulation can be added to the device-test `.json` in an `emulation-file` section.

## Device Tests

Device tests are utilized as part of continuous integration to ensure that all device updates
continue to work as the software stack changes. Device tests will download a payload from the web
(typically from LVFS) and follow the steps to install the firmware. This payload is specified as
an `emulation-url` string parameter in the `steps` section of a specific device test. This causes
the front end to load the emulation data before running the specific step.

Device tests without emulation data will be skipped.

For example:

    fwupdmgr device-emulate ../data/device-tests/hughski-colorhug2.json
    Decompressing…           [***************************************]
    Waiting…                 [***************************************]
    Hughski ColorHug2: OK!
    Decompressing…           [***************************************]
    Waiting…                 [***************************************]
    Hughski ColorHug2: OK!

## Pcap file conversion

Emulation can also be used during the development phase of the plugin if the hardware is not
available or to reduce the number of write cycles on the device and the testing time. But this
requires a way to create the emulation file while fwupd does not yet support the hardware.

This can be done by converting a pcap file generated using WireShark, on Windows or Linux, while
performing the device firmware update process using the official update program. The saved pcap
file should contain all the events of the device plugin before the update starts until it ends.

Since the pcap file contains the firmware uploaded to the device, it is closely related to the
firmware file and both the pcap and firmware files must be provided to the plugin developer.

### Record USB events to pcap file

#### Linux setup

Check if you belong to the wireshark group with:

    groups $USER

To add yourself to the wireshark group, run the below command, then logout and login:

    sudo usermod -a -G wireshark $USER

Depending on the distribution used, you may have to load the USBmon kernel module using:

    sudo modprobe usbmon

You may also need to adjust the permissions with which usbmon instances are created:

    echo 'SUBSYSTEM=="usbmon", GROUP="wireshark", MODE="640"' | sudo tee /etc/udev/rules.d/50-accessible-usbmon.rules
    sudo udevadm control --reload-rules
    sudo udevadm trigger

If USBmon is builtin, you may need to reboot.

### WireShark record of USB events

Start WireShark and open *Capture→Options…* menu (or Ctrl+K). This brings up the *Capture Interfaces*
window then select the USB interface to record packets:

* USBPcap[x] on Windows,
* usbmon[x] on Linux, usbmon0 interface can be used to capture packets on all buses.

When recording firmware update that uses big packets, it may be relevant to increase the packet snaplen
by double clicking on the value in the *Snaplen* column of the selected interface and changing the value.

Click on the *Start* button then plugin the device to record and start the firmware update process.
Once done, save the USB packets from WireShark to a pcap file.

### Convert pcap file to emulation file

To convert this file, the `contrib/pcap2emulation.py` tool is used to generate a json file for each
series of USB events between the "GET DESCRIPTOR DEVICE" events, limited to a set of VendorIDs (and
if necessary ProductIDs).
Depending on the device there should be 2 (setup.json and reload.json) or 3 (setup.json,
install.json and reload.json) phase files, which are zipped in the emulation file.

For example:

    # convert the pcap file for the CalDigit dock with VendorID and ProductID 0451:ace1
    contrib/pcap2emulation.py CalDigit.pcapng /tmp/caldigit 0451:ace1
    # this will generate /tmp/caldigit.zip
    # the new emulation file can be used for emulation
    fwupdmgr emulation-load /tmp/caldigit.zip
    fwupdmgr get-devices --filter emulated
