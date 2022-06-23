---
title: Plugin Tutorial
---

## Introduction

At the heart of fwupd is a plugin loader that gets run at startup, when devices
get hotplugged and when updates are done.
The idea is we have lots of small plugins that each do one thing, and are
ordered by dependencies against each other at runtime.
Using plugins we can add support for new hardware or new policies without making
big changes all over the source tree.

There are broadly 3 types of plugin methods:

- **Mechanism**: Upload binary data into a specific hardware device.
- **Policy**: Control the system when updates are happening, e.g. preventing the
              user from powering-off.
- **Helpers**: Providing more metadata about devices, for instance handling
- device quirks.

In general, building things out-of-tree isn't something that we think is a very
good idea; the API and ABI *internal* to fwupd is still changing and there's a
huge benefit to getting plugins upstream where they can undergo review and be
ported as the API adapts.
For this reason we don't install the plugin headers onto the system, although
you can of course just install the `.so` binary file manually.

A plugin only needs to define the vfuncs that are required, and the plugin name
is taken automatically from the suffix of the `.so` file.

    /*
     * Copyright (C) 2017 Richard Hughes <richard@hughsie.com>
     *
     * SPDX-License-Identifier: LGPL-2.1+
     */

    #include <fwupdplugin.h>

    struct FuPluginData {
        gpointer proxy;
    };

    static void
    fu_plugin_foo_init(FuPlugin *plugin)
    {
        fu_plugin_add_rule(plugin, FU_PLUGIN_RULE_RUN_BEFORE, "dfu");
        fu_plugin_alloc_data(plugin, sizeof(FuPluginData));
    }

    static void
    fu_plugin_foo_destroy(FuPlugin *plugin)
    {
        FuPluginData *data = fu_plugin_get_data(plugin);
        destroy_proxy(data->proxy);
    }

    static gboolean
    fu_plugin_foo_startup(FuPlugin *plugin, FuProgress *progress, GError **error)
    {
        FuPluginData *data = fu_plugin_get_data(plugin);
        data->proxy = create_proxy();
        if(data->proxy == NULL) {
            g_set_error(error, FWUPD_ERROR, FWUPD_ERROR_NOT_SUPPORTED,
                        "failed to create proxy");
            return FALSE;
        }
        return TRUE;
    }

    void
    fu_plugin_init_vfuncs(FuPluginVfuncs *vfuncs)
    {
        vfuncs->build_hash = FU_BUILD_HASH;
        vfuncs->init = fu_plugin_foo_init;
        vfuncs->destroy = fu_plugin_foo_destroy;
        vfuncs->startup = fu_plugin_foo_startup;
    }

We have to define when our plugin is run in reference to other plugins, in this
case, making sure we run before the `dfu` plugin.

For most plugins it does not matter in what order they are run and this
information is not required.

## Creating an abstract device

This section shows how you would create a device which is exported to the daemon
and thus can be queried and updated by the client software.
The example here is all hardcoded, and a true plugin would have to
derive the details about the `FuDevice` from the hardware, for example reading
data from `sysfs` or `/dev`.

    static gboolean
    fu_plugin_foo_coldplug(FuPlugin *plugin, FuProgress *progress, GError **error)
    {
        g_autoptr(FuDevice) dev = NULL;
        fu_device_set_id(dev, "dummy-1:2:3");
        fu_device_add_guid(dev, "2d47f29b-83a2-4f31-a2e8-63474f4d4c2e");
        fu_device_set_version(dev, "1.2.3");
        fu_device_get_version_lowest(dev, "1.2.2");
        fu_device_get_version_bootloader(dev, "0.1.2");
        fu_device_add_icon(dev, "computer");
        fu_device_add_flag(dev, FWUPD_DEVICE_FLAG_UPDATABLE);
        fu_plugin_device_add(plugin, dev);
        return TRUE;
    }

    void
    fu_plugin_init_vfuncs(FuPluginVfuncs *vfuncs)
    {
        …
        vfuncs->coldplug = fu_plugin_foo_coldplug;
        …
    }

This shows a lot of the plugin architecture in action.
Some notable points:

- The device ID (`dummy-1:2:3`) has to be unique on the system between all
plugins, so including the plugin name as a prefix is probably a good idea.

- The GUID value can be generated automatically using
`fu_device_add_guid(dev,"some-identifier")` but is quoted here explicitly. The
GUID value has to match the `provides` value in the `.metainfo.xml` file for the
firmware update to succeed.

- Setting a display name and an icon is a good idea in case the GUI software
needs to display the device to the user. Icons can be specified using a full
path, although icon theme names should be preferred for most devices.

- The `FWUPD_DEVICE_FLAG_UPDATABLE` flag tells the client code that the device
is in a state where it can be updated. If the device needs to be in a special
mode (e.g. a bootloader) then the `FWUPD_DEVICE_FLAG_NEEDS_BOOTLOADER` flag can
also be used. If the update should only be allowed when there is AC power
available to the computer (i.e. not on battery) then
`FWUPD_DEVICE_FLAG_REQUIRE_AC` should be used as well. There are other flags and
the API documentation should be used when choosing what flags to use for each
kind of device.

- Setting the lowest allows client software to refuse downgrading the device to
specific versions.
This is required in case the upgrade migrates some kind of data-store so as to
be incompatible with previous versions.
Similarly, setting the version of the bootloader (if known) allows the firmware
to depend on a specific bootloader version, for instance allowing signed
firmware to only be installable on hardware with a bootloader new enough to
deploy it.

## Mechanism Plugins

Although it would be a wonderful world if we could update all hardware using a
standard shared protocol this is not the universe we live in.
Using a mechanism like DFU or UpdateCapsule means that fwupd will just work
without requiring any special code, but for the real world we need
to support vendor-specific update protocols with layers of backwards compatibility.

When a plugin has created a device that is `FWUPD_DEVICE_FLAG_UPDATABLE` we can
ask the daemon to update the device with a suitable `.cab` file.
When this is done the daemon checks the update for compatibility with the device,
and then calls the vfuncs to update the device.

    static gboolean
    fu_plugin_foo_write_firmware(FuPlugin *plugin,
                                 FuDevice *dev,
                                 GBytes *blob_fw,
                                 FuProgress *progress,
                                 FwupdInstallFlags flags,
                                 GError **error)
    {
        gsize sz = 0;
        guint8 *buf = g_bytes_get_data(blob_fw, &sz);
        /* write 'buf' of size 'sz' to the hardware */
        return TRUE;
    }

    void
    fu_plugin_init_vfuncs(FuPluginVfuncs *vfuncs)
    {
        …
        vfuncs->write_firmware = fu_plugin_foo_write_firmware;
        …
    }

It's important to note that the `blob_fw` is the binary firmware file
(e.g. `.dfu`) and **not** the `.cab` binary data.

If `FWUPD_INSTALL_FLAG_FORCE` is used then the usual checks done by the flashing
process can be relaxed (e.g. checking for quirks), but please don't brick the
users hardware even if they ask you to.

## Policy Helpers

For some hardware, we might want to do an action before or after the actual
firmware is squirted into the device.
This could be something as simple as checking the system battery level is over a
certain threshold, or it could be as complicated as ensuring a vendor-specific
GPIO is asserted when specific types of hardware are updated.

    static gboolean
    fu_plugin_foo_prepare(FuPlugin *plugin, FuDevice *device, GError **error)
    {
        if (fu_device_has_flag(device, FWUPD_DEVICE_FLAG_REQUIRE_AC && !on_ac_power()) {
                g_set_error_literal(error,
                                    FWUPD_ERROR,
                                    FWUPD_ERROR_AC_POWER_REQUIRED,
                                    "Cannot install update "
                                    "when not on AC power");
                return FALSE;
        }
        return TRUE;
    }

    static gboolean
    fu_plugin_foo_cleanup(FuPlugin *plugin, FuDevice *device, GError **error)
    {
        return g_file_set_contents("/var/lib/fwupd/something",
                                   fu_device_get_id(device), -1, error);
    }

    void
    fu_plugin_init_vfuncs(FuPluginVfuncs *vfuncs)
    {
        …
        vfuncs->prepare = fu_plugin_foo_prepare;
        vfuncs->cleanup = fu_plugin_foo_cleanup;
        …
    }

## Detaching to bootloader mode

Some hardware can only be updated in a special bootloader mode, which for most
devices can be switched to automatically.
In some cases the user to do something manually, for instance re-inserting the
hardware with a secret button pressed.

Before the device update is performed the fwupd daemon runs an optional
`update_detach()` vfunc which switches the device to bootloader mode.

After the update (or if the update fails) an the daemon runs an optional
`update_attach()` vfunc which should switch the hardware back to runtime mode.
Finally an optional `update_reload()` vfunc is run to get the new firmware
version from the hardware.

The optional vfuncs are **only** run on the plugin currently registered to
handle the device ID, although the registered plugin can change during the
attach and detach phases.

    static gboolean
    fu_plugin_foo_detach(FuPlugin *plugin, FuDevice *device, FuProgress *progress, GError **error)
    {
        if (hardware_in_bootloader)
            return TRUE;
        return _device_detach(device, progress, error);
    }

    static gboolean
    fu_plugin_foo_attach(FuPlugin *plugin, FuDevice *device, FuProgress *progress, GError **error)
    {
        if (!hardware_in_bootloader)
            return TRUE;
        return _device_attach(device, progress, error);
    }

    static gboolean
    fu_plugin_foo_reload(FuPlugin *plugin, FuDevice *device, GError **error)
    {
        g_autofree gchar *version = _get_version(plugin, device, error);
        if (version == NULL)
            return FALSE;
        fu_device_set_version(device, version);
        return TRUE;
    }

    void
    fu_plugin_init_vfuncs(FuPluginVfuncs *vfuncs)
    {
        …
        vfuncs->detach = fu_plugin_foo_detach;
        vfuncs->attach = fu_plugin_foo_attach;
        vfuncs->reload = fu_plugin_foo_reload;
        …
    }

## The Plugin Object Cache

The fwupd daemon provides a per-plugin cache which allows objects to be added,
removed and queried using a specified key.
Objects added to the cache must be `GObject`s to enable the cache objects to be
properly refcounted.

## Debugging a Plugin

If the fwupd daemon is started with `--plugin-verbose=$plugin` then the
environment variable `FWUPD_$PLUGIN_VERBOSE` is set process-wide.
This allows plugins to detect when they should output detailed debugging
information that would normally be too verbose to keep in the journal.
For example, using `--plugin-verbose=logitech_hidpp` would set
`FWUPD_LOGITECH_HID_VERBOSE=1`.

## Using existing code to develop a plugin

It is not usually possible to share a plugin codebase with firmware update
programs designed for other operating systems.

Matching the same rationale as the Linux kernel, trying to use one code base
between projects with a compatibility shim layer in-between is real headache to
maintain.

The general consensus is that trying to use a abstraction layer for hardware is
a very bad idea as you're not able to take advantage of the platform specific
helpers -- for instance quirk files and the custom GType device creation.

The time the vendor saves by creating a shim layer and importing existing source
code into fwupd will be overtaken 100x by upstream maintenance costs longer term,
which isn't fair.

In a similar way, using C++ rather than GObject C means expanding the test matrix
to include clang in C++ mode and GNU g++ too.
It's also doubled the runtime requirements to now include both the C standard library
as well as the C++ standard library and increases the dependency surface.

Most rewritten fwupd plugins at up to x10 smaller than the standalone code as they
can take advantage of helpers provided by fwupd rather than re-implementing error
handling, device quirking and data chunking.

## General guidelines for plugin developers

### General considerations

When adding support for a new device in fwupd some things need to be
evaluated beforehand:

- how the hardware is discovered, identified and polled.
- how to communicate with the device (USB? file open/read/write?)
- does the device need to be switched to bootloader mode to make it
  upgradable?
- about the format of the firmware files, do they follow any standard?
  are they already supported in fwupd?
- about the update protocol, is it already supported in fwupd?
- Is the device composed of multiple different devices? Are those
  devices enumerated and programmed independently or are they accessed
  and flashed through a "root" device?

In most cases, even if the features you need aren't implemented yet,
there's already a plugin that does something similar and can be used as
an example, so it's always a good idea to read the code of the existing
plugins to understand how they work and how to write a new one, as no
documentation will be as complete and updated as the code
itself. Besides, the mechanisms implemented in the plugin collection are
very diverse and the best way of knowing what can be done is to check
what is already been done.

### Leveraging existing fwupd code

Depending on how much of the key items for the device update (firmware
format, update protocol, transport layer) are already supported in
fwupd, the work needed to add support for a new device can range from
editing a quirk file to having to fully implement new device and
firmware types, although in most cases fwupd already implements helper
code that can be extended.

#### If the firmware format, update protocol and device communication are already supported

This is the simplest case, where an existing plugin fully implements the
update process for the new device and we only have to let fwupd know
that that plugin should be used for our device. In this case the only
thing to do is to edit the plugin quirk file and add the device
identifier in the format expected by the plugin together with any
required options for it (at least a "Plugin" key to declare that this is
the plugin to use for this device). Example:
<https://github.com/fwupd/fwupd/blob/main/plugins/vli/vli-usbhub.quirk>

#### If the device type is not supported

Then we have to take a look at the existing device types and check if
there's any of them that have similarities and which can be partially
reused or extended for our device. If the device type is derivable and
it can support our new device by implementing the proper vfuncs, then we
can simply subclass it and add the required functionalities. If not,
we'll need to study what is the best way to reuse it for our needs.

If a plugin already implements most of the things we need besides the
device type, we can add our new device type to that plugin. Otherwise we
should create a plugin that will hold the new device type.

The core fwupd code contains some basic device types (such as
[FuUdevDevice](https://github.com/fwupd/fwupd/blob/main/libfwupdplugin/fu-udev-device.c), [FuUsbDevice](https://github.com/fwupd/fwupd/blob/main/libfwupdplugin/fu-usb-device.c), [FuBluezDevice](https://github.com/fwupd/fwupd/blob/main/libfwupdplugin/fu-bluez-device.c)) that can be used as a base
type for most devices in case we have to implement our own device
access, identification and communication from scratch.

If the device is natively visible by the OS, most of the time fwupd can
detect the device connection and disconnection by listening to udev
events, but a supported device may also be not directly accessible from
the OS -- for example, a composite device that contains an updatable chip
that's connected through I2C to a USB hub that acts as an interface. In
that case, the device discovery and enumeration must be programmed by
the developer, but the same device identification and management
mechanisms apply in all cases. See the "Creating a new device type" and
"Device identification" below for more details.

#### If the firmware type is not supported

Same as with the new device type, there could be an existing firmware
type that can be used as a base type for our new type, so first of all
we should look for firmware types that are similar to the one we're
using. Then, choosing where to define the new type depends on whether
there's already a plugin that implements most of the functionalities we
need or not.

### Example: extending a firmware type

Our firmware files are Intel HEX files that have optional
vendor-specific sections at fixed addresses, this is not supported by
any firmware type in fwupd out of the box but the [FuIhexFirmare](https://github.com/fwupd/fwupd/blob/main/libfwupdplugin/fu-ihex-firmware.c) class
parses and models a standard Intel HEX file, so we can create a subclass
of it for our firmware type and override the parse method so that it
calls the method from the parent class, which would parse the file, and
then we can get the data with `fu_firmware_get_bytes()` and do the rest of
the custom parsing. Example:
<https://github.com/fwupd/fwupd/blob/main/plugins/analogix/fu-analogix-firmware.c>

### Example: extending a device type

Communication with our new device is carried out by doing
read/write/ioctl operations on a device file, but using a custom
protocol that is not supported in fwupd.

For this type of device we can create a new type derived from
`FuUdevDevice`, which takes care of discovering this type of devices,
possibly using a vendor-specific protocol, as well as of opening,
reading and writing device files, so we would only have to implement the
protocol on top of those primitives. (Example:
`fu_logitech_hidpp_runtime_bolt_poll_peripherals()` in
<https://github.com/fwupd/fwupd/blob/main/plugins/logitech-hidpp/fu-logitech-hidpp-runtime-bolt.c>)
The process would be similar if our device was handled by a different
backend (USB or BlueZ).

### Creating a new plugin

The bare minimum a plugin should have is a `fu_plugin_init` function that
defines the plugin characteristics such as the device type and firmware
type handled by it, the build hash and any plugin-specific quirk keys
that can be used for the plugin.

    void
    fu_plugin_steelseries_init(FuPlugin *plugin)
    {
        FuContext *ctx = fu_plugin_get_context(plugin);
        fu_plugin_add_device_gtype(plugin, FU_TYPE_STEELSERIES_MOUSE);
        fu_plugin_add_device_gtype(plugin, FU_TYPE_STEELSERIES_GAMEPAD);
    }

    void
    fu_plugin_init_vfuncs(FuPluginVfuncs *vfuncs)
    {
        vfuncs->build_hash = FU_BUILD_HASH;
        vfuncs->init = fu_plugin_steelseries_init;
    }

### Creating a new device type

Besides defining its attributes as a data type, a device type should
implement at least the usual `init`, `finalize` and `class_init` functions,
and then, depending on its parent type, which methods it overrides and
what it does, it must implement a set of device methods. These are some
of them, the complete list is in [libfwupdplugin/fu-device.h](https://github.com/fwupd/fwupd/blob/main/libfwupdplugin/fu-device.h).

#### to_string

Called whenever fwupd needs a human-readable representation of the
device.

#### probe

The `probe` method is called the first time a device is opened, before
actually opening it. The generic probe methods implemented in the base
device types (such as USB/udev) take care of basic device identification
and setting the non-specific parameters that don't need the device to be
opened or the interface claimed (vendor id, product id, guids, etc.).

The device-specific probe method should start by calling the generic
method upwards in the class tree and then do any other specific setup
such as setting the appropriate device flags.

#### open

Depending on the type of device, opening it means different things. For
instance, opening a udev device means opening its device file.

If there's no interface-specific `open` method, then opening a device
simply calls the `probe()` and `setup()` methods (the `open()` method would be
called in between if it exists).

#### setup

Sets parameters on the device object that require the device to be open
and have the interface claimed. USB/udev generic devices don't implement
this method, this is normally implemented for each different plugin
device type if needed.

#### prepare

If implemented, this takes care of decompressing or parsing the firmware
data. For example, to check if the firmware is valid, if it's suitable
for the device, etc.

It takes a stream of bytes (`GBytes`) as a parameter, representing the
raw binary firmware data.

It should create the firmware object and call the appropriate method to
load the firmware. Otherwise, if it's not implemented for the specific
device type, the generic implementation in
[libfwupdplugin/fu-device.c](https://github.com/fwupd/fwupd/blob/main/libfwupdplugin/fu-device.c):`fu_device_prepare_firmware()`
creates a firmware object loaded with a provided image.

#### detach

Implemented if the device needs to be put in bootloader mode before
updating, this does all the necessary operations to put the device in
that mode. fwupd can handle the case where a device needs to be
disconnected to do the mode switch if the device has the
`FWUPD_DEVICE_FLAG_WAIT_FOR_REPLUG` flag.

#### attach

The inverse of `detach()`, to configure the device back to application mode.

#### reload

If implemented, this is called after the device update if it needs to
perform any kind of post-update operation.

#### write_firmware

Writes a firmware passed as a raw byte stream. The firmware parsing and
processing is done by the firmware object, so that when this method gets
the blob it simply has to write it to the device in the appropriate way
following the device update protocol.

#### read_firmware

Reads the firmware data from the device without any device-specific
configuration or serial numbers. This is meant to retrieve the current
firmware contents for verification purposes. The data read can then be
output to a binary blob using `fu_firmware_write()`.

#### set_progress

Informs the daemon of the expected duration percentages for the different
phases of update. The daemon runs the `->detach()`, `->write_firmware()`,
`->attach()` and `->reload()` phases as part of the engine during the firmware
update (rather than being done by plugin-specific code) and so this vfunc
informs the daemon how to scale the progress output accordingly.

For instance, if your update takes 2 seconds to detach into bootloader mode,
10 seconds to write the firmware, 7 seconds to attach back into runtime mode
(which includes the time required for USB enumeration) and then 1 second to
read the new firmware version you would use:

    fu_progress_set_id(progress, G_STRLOC);
    fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_RESTART, 10, "detach");
    fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_WRITE, 45, "write");
    fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_RESTART, 40, "attach");
    fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_BUSY, 5, "reload");

If however your device does not require `->detach()` or `->attach()`, and
`->reload()` is instantaneous, you still however need to include 4 steps:

    fu_progress_set_id(progress, G_STRLOC);
    fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_RESTART, 0, "detach");
    fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_WRITE, 100, "write");
    fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_RESTART, 0, "attach");
    fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_BUSY, 0, "reload");

If the device has multiple phases that occur when actually in the write phase
then it is perfectly okay to split up the `FuProgress` steps in the
`->write_firmware()` vfunc further. For instance:

    fu_progress_set_id(progress, G_STRLOC);
    fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_RESTART, 5, "wait-for-idle");
    fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_WRITE, 90, "write");
    fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_RESTART, 5, "reset");

It should be noted that actions that are required to be done *before* the update
should be added as a `->prepare()` vfunc, and those to be done after in the `->cleanup()`
as the daemon will then recover the hardware if the update fails. For instance,
putting the device back into a *normal runtime power saving* state should always
be done during cleanup.

### Creating a new firmware type

The same way a device type implements some methods to complete its
functionality and override certain behaviors, there's a set of firmware
methods that a firmware class can (or must) implement:

#### parse

If implemented, it parses the firmware file passed as a byte
sequence. If the firmware to be used contains a custom header, a
specific structured format or multiple images embedded, this method
should take care of processing the format and appropriately populating
the `FuFirmware` object passed as a parameter. If not implemented, the
whole data blob is taken as is.

#### write

Returns a `FuFirmware` object as a byte sequence. This can be used to
output a firmware read with `fu_device_read_firmware()` as a binary
blob.

#### export

Converts a `FuFirmware` object to an xml representation. If not
implemented, the default implementation generates an xml representation
containing only generic attributes and, optionally, the firmware data as
well as the representation of children firmware nodes.

When testing the implementation of a new firmware type, this is useful
to show if the parsing and processing of the firmware are correct and
can be checked with:

    fwupdtool firmware-parse --plugins <plugin> <firmware_file> <firmware_type>

#### tokenize

If implemented it tokenizes a firmware, breaking it into records.

#### build

This is the reverse of `export()`, it builds a `FuFirmware` object from
an xml representation.

#### get_checksum

The default implementation returns a checksum of the payload data of a
`FuFirmware` object. Subclass it only if the checksum of your firmware
needs to be computed differently.

### Device identification

A device is identified in fwupd by its physical and logical ids. A
physical id represents the electrical connection of the device to the
system and many devices can have the same physical id. For example,
`PCI_SLOT_NAME=0000:3e:00:0` (see
[libfwupdplugin/fu-udev-device.c](https://github.com/fwupd/fwupd/blob/main/libfwupdplugin/fu-udev-device.c):`fu_udev_device_set_physical_id()` for
examples) . The logical id is used to disambiguate devices with the same
physical id. Together they identify a device uniquely. There are many
examples of this in the existing plugins, such as
`fu_pxi_receiver_device_add_peripherals()` in
<https://github.com/fwupd/fwupd/blob/main/plugins/pixart-rf/fu-pxi-receiver-device.c>

Besides that, each device type will have a unique instance id, which is
a string representing the device subsystem, vendor, model and revision
(specific details depend on the device type). This should identify a
device type in the system, that is, a particular device type, model and
revision by a specific vendor will have a defined instance id and two of
the same device will have the same instance id (see
[libfwupdplugin/fu-udev-device.c](https://github.com/fwupd/fwupd/blob/main/libfwupdplugin/fu-udev-device.c):`fu_udev_device_probe()`
for examples).

One or more GUIDs are generated for a device from its identifying
attributes, these GUIDs are then used to match a firmware metadata
against a specific device type. See the implementation of the many
`probe()` methods for examples.

### Support for BLE devices

BLE support in fwupd on Linux is provided by BlueZ. If the device
implements the standard HID-over-GATT BLE profile, then communication
with the device can be done through the [hidraw
interface](https://www.kernel.org/doc/html/latest/hid/hidraw.html). If
the device implements a custom BLE profile instead, then it will have to
be managed by the `FuBluezBackend`, which uses the BlueZ DBus interface
to communicate with the devices. The `FuBluezDevice` type implements
device enumeration as well as the basic primitives to read and write BLE
characteristics, and can be used as the base type for a more specific
BLE device.

### Battery checks

If the device can be updated wirelessly or if the update process doesn't
rely on an external power supply, the vendor might define a minimum
operative battery level to guarantee a correct update. fwupd provides a
simple API to define these requirements per-device.

[fu_device_set_battery_threshold()](https://github.com/fwupd/fwupd/blob/main/libfwupdplugin/fu-device.c)
can be used to define the minimum battery level required to allow a
firmware update on a device (10% by default). If the battery level is
below that threshold, fwupd will inhibit the device to prevent the user
from starting a firmware update. Then, the battery level of a device can
be queried and then set with
[fu_device_set_battery_level()](https://github.com/fwupd/fwupd/blob/main/libfwupdplugin/fu-device.c).

## Howtos

### How to create a child device

fwupd devices can be hierarchically ordered to model dependent and
composite devices such as docking stations composed of multiple
updatable chips. When writing support for a new composite device the
parent device should, at some point, poll the devices that "hang" from
it and register them in fwupd. The process of polling and identifying a
child device is totally vendor and device-specific, although the main
requirement for it is that the child device is properly identified
(having physical/logical and instance ids). Then,
[fu_device_add_child()](https://github.com/fwupd/fwupd/blob/main/libfwupdplugin/fu-device.c)
can be used to add a new child device to an existing one. See
`fu_logitech_hidpp_runtime_bolt_poll_peripherals()` in
<https://github.com/fwupd/fwupd/blob/main/plugins/logitech-hidpp/fu-logitech-hidpp-runtime-bolt.c>
for an example.

Note that when deploying and installing a firmware set for a composite
device, there might be firmware dependencies between parent and child
devices that require a specific update ordering (for instance, child
devices first, then the parent). This can be modeled by setting an
appropriate firmware priority in the firmware metainfo or by setting the
`FWUPD_DEVICE_FLAG_INSTALL_PARENT_FIRST` device flag.

### How to add a delay

In certain scenarios you may need to introduce small controlled delays
in the plugin code, for instance, to comply with a communications
protocol or to wait for the device to be ready after a particular
operation. In this case you can insert a delay in microseconds with
`g_usleep` or a delay in seconds that shows a progress bar with
`fu_device_sleep_with_progress`. Note that, in both cases, this will
stop the application main loop during the wait, so use it only when
necessary.

### How to define private flags

Besides the regular flags and internal flags that any device can have, a
device can define private flags for specific uses. These can be enabled
in the code as well as in quirk files, just as the rest of flags. To
define a private flag:

1. Define the flag value. This is normally defined as a macro that
  expands to a binary flag, for example: `#define MY_PRIVATE_FLAG (1 <<
  2)`.  Note that this will be part of the ABI, so it must be versioned
1. Call `fu_device_register_private_flag` in the device init function
  and assign a string identifier to the flag:
  `fu_device_register_private_flag (FU_DEVICE (self), MY_PRIVATE_FLAG,
  "myflag");`

You can then add it to the device programmatically with
`fu_device_add_private_flag`, remove it with `fu_device_remove_private_flag`
and query it with `fu_device_has_private_flag`. In a quirk file, you can
add the flag identifier to the Flags attribute of a device (eg. `Flags =
myflag,is-bootloader`)

### How to make fwupd wait for a device replug

Certain devices require a disconnection and reconnection to start the
update process. A common example are devices that have two booting
modes: application or runtime mode, and bootloader mode, where the
runtime mode is the normal operation mode and the bootloader mode is
exclusively used to update the device firmware. It's common for these
devices to require some operation from fwupd to switch the booting mode
and then to need a reset to enter bootloader mode. Often, the device is
enumerated differently in both modes, so fwupd needs to know that the
same device will be identified differently depending on the boot mode.

The common way to do this is to add the
`FWUPD_DEVICE_FLAG_WAIT_FOR_REPLUG` flag in the device before its detach
method returns. This will make fwupd wait for a predetermined amount of
time for the device to be detected again. Then, to inform fwupd about
the two identities of the same device, the `CounterpartGuid` key can be
used in a device entry to match it with another defined device (example:
<https://github.com/fwupd/fwupd/blob/main/plugins/steelseries/steelseries.quirk>).

### Inhibiting a device

If a device becomes unsuitable for an update for whatever reason (see
"Battery checks" above for an example), a plugin can temporarily disable
firmware updates on it by calling [fu_device_inhibit()](https://github.com/fwupd/fwupd/blob/main/libfwupdplugin/fu-device.c). The device will
still be listed as present by `fwupdmgr get-devices`, but fwupd won't
allow firmware updates on it.  Device inhibition can be disabled with
[fu_device_uninhibit()](https://github.com/fwupd/fwupd/blob/main/libfwupdplugin/fu-device.c).

Note that there might be multiple inhibits on a specific device, the
device will only be updatable when all of them are removed.

## Debugging tips

The most important rule when debugging is using the `--verbose` flag
when running fwupd or fwupdtool. Besides that, there are many
environment variables that allow some debug traces to be printed
conditionally, for example: `FWUPD_PROBE_VERBOSE`,
`FU_HID_DEVICE_VERBOSE`, `FWUPD_DEVICE_LIST_VERBOSE` and many other
plugin-specific envvars.

### Adding debug messages

The usual way to print a debug message is using the `g_debug` macro. Each
relevant module will define its own `G_LOG_DOMAIN` to tag the debug traces
accordingly. See
<https://docs.gtk.org/glib/logging.html> and
<https://docs.gtk.org/glib/running.html> for more
information.

### Inspecting raw binary data

The `fu_dump_full` and `fu_dump_raw` functions implement the
printing of a binary buffer to the console as a stream of bytes in
hexadecimal. See `libfwupdplugin/fu-common.c` for their definitions, you
can find many examples of how to use them in the plugins code.
