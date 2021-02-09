/*
 * Copyright (C) 2016 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#pragma once

#include "fu-plugin.h"
#include "fu-device.h"
#include "fu-security-attrs.h"

/* for in-tree plugins only */
#ifdef FWUPD_COMPILATION
#include "fu-hash.h"
/* only until HSI is declared stable */
#include "fwupd-security-attr-private.h"
#endif

/**
 * SECTION:fu-plugin-vfuncs
 * @short_description: Virtual functions for plugins
 *
 * Optional functions that a plugin can implement.  If implemented they will
 * be automatically called by the daemon as part of the plugin lifecycle.
 *
 * See also: #FuPlugin
 */


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
 * @plugin: A #FuPlugin
 *
 * Destroys the plugin.
 * Any allocated memory should be freed here.
 *
 * Since: 0.8.0
 **/
void		 fu_plugin_destroy			(FuPlugin	*plugin);

/**
 * fu_plugin_startup:
 * @plugin: A #FuPlugin
 * @error: A #GError
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
 * @plugin: A #FuPlugin
 * @error: A #GError
 *
 * Probes for devices.
 *
 * Since: 0.8.0
 **/
gboolean	 fu_plugin_coldplug			(FuPlugin	*plugin,
							 GError		**error);
/**
 * fu_plugin_coldplug_prepare:
 * @plugin: A #FuPlugin
 * @error: A #GError
 *
 * Prepares to probe for devices.
 *
 * Since: 0.8.0
 **/
gboolean	 fu_plugin_coldplug_prepare		(FuPlugin	*plugin,
							 GError		**error);
/**
 * fu_plugin_coldplug_cleanup:
 * @plugin: A #FuPlugin
 * @error: A #GError
 *
 * Cleans up from probe for devices.
 *
 * Since: 0.8.0
 **/
gboolean	 fu_plugin_coldplug_cleanup		(FuPlugin	*plugin,
							 GError		**error);
/**
 * fu_plugin_recoldplug:
 * @plugin: A #FuPlugin
 * @error: A #GError or NULL
 *
 * Re-runs the coldplug routine for devices.
 *
 * Since: 1.0.4
 **/
gboolean	 fu_plugin_recoldplug			(FuPlugin	*plugin,
							 GError		**error);
/**
 * fu_plugin_update:
 * @plugin: A #FuPlugin
 * @dev: A #FuDevice
 * @blob_fw: A #GBytes
 * @flags: A #FwupdInstallFlags
 * @error: A #GError or NULL
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
 * @plugin: A #FuPlugin
 * @dev: A #FuDevice
 * @flags: A #FuPluginVerifyFlags
 * @error: A #GError or NULL
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
 * @plugin: A #FuPlugin
 * @dev: A #FuDevice
 * @error: A #GError or NULL
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
 * @plugin: A #FuPlugin
 * @dev: A #FuDevice
 * @error: A #GError or NULL
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
 * @plugin: A #FuPlugin
 * @dev: A #FuDevice
 * @error: A #GError or NULL
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
 * @plugin: A #FuPlugin
 * @dev: A #FuDevice
 * @error: A #GError or NULL
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
 * @plugin: A #FuPlugin
 * @dev: A #FuDevice
 * @error: A #GError or NULL
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
 * @plugin: A #FuPlugin
 * @dev: A #FuDevice
 * @error: A #GError or NULL
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
 * @plugin: A #FuPlugin
 * @flags: A #FwupdInstallFlags
 * @dev: A #FuDevice
 * @error: A #GError or NULL
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
 * @plugin: A #FuPlugin
 * @flags: A #FwupdInstallFlags
 * @dev: A #FuDevice
 * @error: A #GError or NULL
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
 * @plugin: A #FuPlugin
 * @devices: A #GPtrArray of #FuDevice
 * @error: A #GError or NULL
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
 * @plugin: A #FuPlugin
 * @devices: A #GPtrArray of #FuDevice
 * @error: A #GError or NULL
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
 * @plugin: A #FuPlugin
 * @device: A #FuDevice
 * @error: A #GError or NULL
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
 * @plugin: A #FuPlugin
 * @device: A #FuDevice
 * @error: A #GError or NULL
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
 * @plugin: A #FuPlugin
 * @device: A #FuDevice
 * @error: A #GError or NULL
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
 * @plugin: A #FuPlugin
 * @dev: A #FuDevice
 *
 * Function run when the subclassed device has been added.
 *
 * Since: 1.5.0
 **/
void		 fu_plugin_device_added			(FuPlugin	*plugin,
							 FuDevice	*dev);
/**
 * fu_plugin_device_created
 * @plugin: A #FuPlugin
 * @dev: A #FuDevice
 * @error: A #GError or %NULL
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
 * @plugin: A #FuPlugin
 * @dev: A #FuDevice
 *
 * Function run when device registered from another plugin.
 *
 * Since: 0.9.7
 **/
void		 fu_plugin_device_registered		(FuPlugin	*plugin,
							 FuDevice	*dev);
/**
 * fu_plugin_add_security_attrs
 * @plugin: A #FuPlugin
 * @attrs: A #FuSecurityAttrs
 *
 * Function that asks plugins to add Host Security Attributes.
 *
 * Since: 1.5.0
 **/
void		 fu_plugin_add_security_attrs		(FuPlugin	*plugin,
							 FuSecurityAttrs *attrs);
