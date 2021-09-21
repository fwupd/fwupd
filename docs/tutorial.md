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
     * Copyright (C) 2017 Richard Hughes
     */

    #include <fu-plugin.h>
    #include <fu-plugin-vfuncs.h>

    struct FuPluginData {
        gpointer proxy;
    };

    void
    fu_plugin_initialize (FuPlugin *plugin)
    {
        fu_plugin_add_rule (plugin, FU_PLUGIN_RULE_RUN_BEFORE, "dfu");
        fu_plugin_alloc_data (plugin, sizeof (FuPluginData));
    }

    void
    fu_plugin_destroy (FuPlugin *plugin)
    {
        FuPluginData *data = fu_plugin_get_data (plugin);
        destroy_proxy (data->proxy);
    }

    gboolean
    fu_plugin_startup (FuPlugin *plugin, GError **error)
    {
        FuPluginData *data = fu_plugin_get_data (plugin);
        data->proxy = create_proxy ();
        if (data->proxy == NULL) {
            g_set_error (error, FWUPD_ERROR, FWUPD_ERROR_NOT_SUPPORTED,
                         "failed to create proxy");
            return FALSE;
        }
        return TRUE;
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

    #include <fu-plugin.h>

    gboolean
    fu_plugin_coldplug (FuPlugin *plugin, GError **error)
    {
        g_autoptr(FuDevice) dev = NULL;
        fu_device_set_id (dev, "dummy-1:2:3");
        fu_device_add_guid (dev, "2d47f29b-83a2-4f31-a2e8-63474f4d4c2e");
        fu_device_set_version (dev, "1.2.3");
        fu_device_get_version_lowest (dev, "1.2.2");
        fu_device_get_version_bootloader (dev, "0.1.2");
        fu_device_add_icon (dev, "computer");
        fu_device_add_flag (dev, FWUPD_DEVICE_FLAG_UPDATABLE);
        fu_plugin_device_add (plugin, dev);
        return TRUE;
    }

This shows a lot of the plugin architecture in action.
Some notable points:

- The device ID (`dummy-1:2:3`) has to be unique on the system between all
- plugins, so including the plugin name as a prefix is probably a good idea.

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

    gboolean
    fu_plugin_write_firmware (FuPlugin *plugin,
                      FuDevice *dev,
                      GBytes *blob_fw,
                      FuProgress *progress,
                      FwupdInstallFlags flags,
                      GError **error)
    {
        gsize sz = 0;
        guint8 *buf = g_bytes_get_data (blob_fw, &sz);
        /* write 'buf' of size 'sz' to the hardware */
        return TRUE;
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

    gboolean
    fu_plugin_prepare (FuPlugin *plugin, FuDevice *device, GError **error)
    {
        if (fu_device_has_flag (device, FWUPD_DEVICE_FLAG_REQUIRE_AC &&
            !on_ac_power ()) {
                g_set_error_literal (error,
                                     FWUPD_ERROR,
                                     FWUPD_ERROR_AC_POWER_REQUIRED,
                                     "Cannot install update "
                                     "when not on AC power");
                return FALSE;
        }
        return TRUE;
    }

    gboolean
    fu_plugin_cleanup (FuPlugin *plugin, FuDevice *device, GError **error)
    {
        return g_file_set_contents ("/var/lib/fwupd/something",
                                    fu_device_get_id (device), -1, error);
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

    gboolean
    fu_plugin_detach (FuPlugin *plugin, FuDevice *device, FuProgress *progress, GError **error)
    {
        if (hardware_in_bootloader)
            return TRUE;
        return _device_detach(device, progress, error);
    }

    gboolean
    fu_plugin_attach (FuPlugin *plugin, FuDevice *device, FuProgress *progress, GError **error)
    {
        if (!hardware_in_bootloader)
            return TRUE;
        return _device_attach(device, progress, error);
    }

    gboolean
    fu_plugin_reload (FuPlugin *plugin, FuDevice *device, GError **error)
    {
        g_autofree gchar *version = _get_version(plugin, device, error);
        if (version == NULL)
            return FALSE;
        fu_device_set_version(device, version);
        return TRUE;
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
