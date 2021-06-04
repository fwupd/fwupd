/*
 * Copyright (C) 2016 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#pragma once

#include "fu-plugin.h"
#include "fu-device.h"
#include "fu-hwids.h"
#include "fu-quirks.h"
#include "fu-security-attrs.h"

/* for in-tree plugins only */
#ifdef FWUPD_COMPILATION
#include "fu-hash.h"
/* only until HSI is declared stable */
#include "fwupd-security-attr-private.h"
#endif

/**
 * fu_plugin_init:
 * @plugin: A #FuPlugin
 *
 * Initializes the plugin.
 * Sets up any static data structures for the plugin.
 * Most plugins should call fu_plugin_set_build_hash in here.
 *
 * Since: 0.8.0
 **/
void		 fu_plugin_init				(FuPlugin	*plugin);

/**
 * fu_plugin_destroy:
 * @plugin: a plugin
 *
 * Destroys the plugin.
 * Any allocated memory should be freed here.
 *
 * Since: 0.8.0
 **/
void		 fu_plugin_destroy			(FuPlugin	*plugin);

/**
 * fu_plugin_startup:
 * @plugin: a plugin
 * @error: (nullable): optional return location for an error
 *
 * Tries to start the plugin.
 * Returns: TRUE for success or FALSE for failure.
 *
 * Any plugins not intended for the system or that have failure communicating
 * with the device should return FALSE.
 * Any allocated memory should be freed here.
 *
 * Since: 0.8.0
 **/
gboolean	 fu_plugin_startup			(FuPlugin	*plugin,
							 GError		**error);
/**
 * fu_plugin_coldplug:
 * @plugin: a plugin
 * @error: (nullable): optional return location for an error
 *
 * Probes for devices.
 *
 * Since: 0.8.0
 **/
gboolean	 fu_plugin_coldplug			(FuPlugin	*plugin,
							 GError		**error);
/**
 * fu_plugin_coldplug_prepare:
 * @plugin: a plugin
 * @error: (nullable): optional return location for an error
 *
 * Prepares to probe for devices.
 *
 * Since: 0.8.0
 **/
gboolean	 fu_plugin_coldplug_prepare		(FuPlugin	*plugin,
							 GError		**error);
/**
 * fu_plugin_coldplug_cleanup:
 * @plugin: a plugin
 * @error: (nullable): optional return location for an error
 *
 * Cleans up from probe for devices.
 *
 * Since: 0.8.0
 **/
gboolean	 fu_plugin_coldplug_cleanup		(FuPlugin	*plugin,
							 GError		**error);
/**
 * fu_plugin_update:
 * @plugin: a plugin
 * @dev: a device
 * @blob_fw: a data blob
 * @flags: install flags
 * @error: (nullable): optional return location for an error
 *
 * Updates the firmware on the device with blob_fw
 *
 * Since: 0.9.7
 **/
gboolean	 fu_plugin_update			(FuPlugin	*plugin,
							 FuDevice	*dev,
							 GBytes		*blob_fw,
							 FwupdInstallFlags flags,
							 GError		**error);
/**
 * fu_plugin_verify:
 * @plugin: a plugin
 * @dev: a device
 * @flags: verify flags
 * @error: (nullable): optional return location for an error
 *
 * Verifies the firmware on the device matches the value stored in the database
 *
 * Since: 0.8.0
 **/
gboolean	 fu_plugin_verify			(FuPlugin	*plugin,
							 FuDevice	*dev,
							 FuPluginVerifyFlags flags,
							 GError		**error);
/**
 * fu_plugin_unlock:
 * @plugin: a plugin
 * @dev: a device
 * @error: (nullable): optional return location for an error
 *
 * Unlocks the device for writes.
 *
 * Since: 0.8.0
 **/
gboolean	 fu_plugin_unlock			(FuPlugin	*plugin,
							 FuDevice	*dev,
							 GError		**error);
/**
 * fu_plugin_activate:
 * @plugin: a plugin
 * @dev: a device
 * @error: (nullable): optional return location for an error
 *
 * Activates the new firmware on the device.
 *
 * This is intended for devices that it is not safe to immediately activate
 * the firmware.  It may be called at a more convenient time instead.
 *
 * Since: 1.2.6
 **/
gboolean	 fu_plugin_activate			(FuPlugin	*plugin,
							 FuDevice	*dev,
							 GError		**error);
/**
 * fu_plugin_clear_results:
 * @plugin: a plugin
 * @dev: a device
 * @error: (nullable): optional return location for an error
 *
 * Clears stored update results for the device.
 *
 * Since: 0.8.0
 **/
gboolean	 fu_plugin_clear_results		(FuPlugin	*plugin,
							 FuDevice	*dev,
							 GError		**error);
/**
 * fu_plugin_get_results:
 * @plugin: a plugin
 * @dev: a device
 * @error: (nullable): optional return location for an error
 *
 * Obtains historical update results for the device.
 *
 * Since: 0.8.0
 **/
gboolean	 fu_plugin_get_results			(FuPlugin	*plugin,
							 FuDevice	*dev,
							 GError		**error);
/**
 * fu_plugin_update_attach:
 * @plugin: a plugin
 * @dev: a device
 * @error: (nullable): optional return location for an error
 *
 * Swaps the device from bootloader mode to runtime mode.
 *
 * Since: 1.0.2
 **/
gboolean	 fu_plugin_update_attach		(FuPlugin	*plugin,
							 FuDevice	*dev,
							 GError		**error);
/**
 * fu_plugin_update_detach:
 * @plugin: a plugin
 * @dev: a device
 * @error: (nullable): optional return location for an error
 *
 * Swaps the device from runtime mode to bootloader mode.
 *
 * Since: 1.0.2
 **/
gboolean	 fu_plugin_update_detach		(FuPlugin	*plugin,
							 FuDevice	*dev,
							 GError		**error);
/**
 * fu_plugin_update_prepare:
 * @plugin: a plugin
 * @flags: install flags
 * @dev: a device
 * @error: (nullable): optional return location for an error
 *
 * Prepares the device to receive an update.
 *
 * Since: 0.8.0
 **/
gboolean	 fu_plugin_update_prepare		(FuPlugin	*plugin,
							 FwupdInstallFlags flags,
							 FuDevice	*dev,
							 GError		**error);
/**
 * fu_plugin_update_cleanup
 * @plugin: a plugin
 * @flags: install flags
 * @dev: a device
 * @error: (nullable): optional return location for an error
 *
 * Cleans up the device after receiving an update.
 *
 * Since: 0.8.0
 **/
gboolean	 fu_plugin_update_cleanup		(FuPlugin	*plugin,
							 FwupdInstallFlags flags,
							 FuDevice	*dev,
							 GError		**error);
/**
 * fu_plugin_composite_prepare
 * @plugin: a plugin
 * @devices: (element-type FuDevice): array of devices
 * @error: (nullable): optional return location for an error
 *
 * Function run before updating group of composite devices.
 *
 * Since: 1.0.9
 **/
gboolean	 fu_plugin_composite_prepare		(FuPlugin	*plugin,
							 GPtrArray	*devices,
							 GError		**error);
/**
 * fu_plugin_composite_cleanup
 * @plugin: a plugin
 * @devices: (element-type FuDevice): array of devices
 * @error: (nullable): optional return location for an error
 *
 * Function run after updating group of composite devices.
 *
 * Since: 1.0.9
 **/
gboolean	 fu_plugin_composite_cleanup		(FuPlugin	*plugin,
							 GPtrArray	*devices,
							 GError		**error);
/**
 * fu_plugin_backend_device_added
 * @plugin: a plugin
 * @device: a device
 * @error: (nullable): optional return location for an error
 *
 * Function to run after a device is added by a backend, e.g. by USB or Udev.
 *
 * Since: 1.5.6
 **/
gboolean	 fu_plugin_backend_device_added		(FuPlugin	*plugin,
							 FuDevice	*device,
							 GError		**error);
/**
 * fu_plugin_backend_device_changed
 * @plugin: a plugin
 * @device: a device
 * @error: (nullable): optional return location for an error
 *
 * Function run when the device changed.
 *
 * Since: 1.5.6
 **/
gboolean	 fu_plugin_backend_device_changed	(FuPlugin	*plugin,
							 FuDevice	*device,
							 GError		**error);
/**
 * fu_plugin_backend_device_removed
 * @plugin: a plugin
 * @device: a device
 * @error: (nullable): optional return location for an error
 *
 * Function to run when device is physically removed.
 *
 * Since: 1.5.6
 **/
gboolean	 fu_plugin_backend_device_removed	(FuPlugin	*plugin,
							 FuDevice	*device,
							 GError		**error);
/**
 * fu_plugin_device_added
 * @plugin: a plugin
 * @dev: a device
 *
 * Function run when the subclassed device has been added.
 *
 * Since: 1.5.0
 **/
void		 fu_plugin_device_added			(FuPlugin	*plugin,
							 FuDevice	*dev);
/**
 * fu_plugin_device_created
 * @plugin: a plugin
 * @dev: a device
 * @error: (nullable): optional return location for an error
 *
 * Function run when the subclassed device has been created.
 *
 * Since: 1.4.0
 **/
gboolean	 fu_plugin_device_created		(FuPlugin	*plugin,
							 FuDevice	*dev,
							 GError		**error);
/**
 * fu_plugin_device_registered
 * @plugin: a plugin
 * @dev: a device
 *
 * Function run when device registered from another plugin.
 *
 * Since: 0.9.7
 **/
void		 fu_plugin_device_registered		(FuPlugin	*plugin,
							 FuDevice	*dev);
/**
 * fu_plugin_add_security_attrs
 * @plugin: a plugin
 * @attrs: a security attribute
 *
 * Function that asks plugins to add Host Security Attributes.
 *
 * Since: 1.5.0
 **/
void		 fu_plugin_add_security_attrs		(FuPlugin	*plugin,
							 FuSecurityAttrs *attrs);
