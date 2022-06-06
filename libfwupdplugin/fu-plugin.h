/*
 * Copyright (C) 2015 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#pragma once

#include <gio/gio.h>
#ifdef HAVE_GUSB
#include <gusb.h>
#endif

#include "fu-bluez-device.h"
#include "fu-common-guid.h"
#include "fu-common.h"
#include "fu-context.h"
#include "fu-device-locker.h"
#include "fu-device.h"
#include "fu-hwids.h"
#include "fu-plugin.h"
#include "fu-quirks.h"
#include "fu-security-attrs.h"
#include "fu-usb-device.h"
#include "fu-version-common.h"
//#include "fu-hid-device.h"
#ifdef HAVE_GUDEV
#include "fu-udev-device.h"
#endif
#include <libfwupd/fwupd-common.h>
#include <libfwupd/fwupd-plugin.h>

/* for in-tree plugins only */
#ifdef FWUPD_COMPILATION
#include "fu-hash.h"
/* only until HSI is declared stable */
#include "fwupd-security-attr-private.h"
#endif

#define FU_TYPE_PLUGIN (fu_plugin_get_type())
G_DECLARE_DERIVABLE_TYPE(FuPlugin, fu_plugin, FU, PLUGIN, FwupdPlugin)

#define fu_plugin_get_flags(p)	    fwupd_plugin_get_flags(FWUPD_PLUGIN(p))
#define fu_plugin_has_flag(p, f)    fwupd_plugin_has_flag(FWUPD_PLUGIN(p), f)
#define fu_plugin_add_flag(p, f)    fwupd_plugin_add_flag(FWUPD_PLUGIN(p), f)
#define fu_plugin_remove_flag(p, f) fwupd_plugin_remove_flag(FWUPD_PLUGIN(p), f)

struct _FuPluginClass {
	FwupdPluginClass parent_class;
	/* signals */
	void (*device_added)(FuPlugin *self, FuDevice *device);
	void (*device_removed)(FuPlugin *self, FuDevice *device);
	void (*status_changed)(FuPlugin *self, FwupdStatus status);
	void (*percentage_changed)(FuPlugin *self, guint percentage);
	void (*device_register)(FuPlugin *self, FuDevice *device);
	gboolean (*check_supported)(FuPlugin *self, const gchar *guid);
	void (*rules_changed)(FuPlugin *self);
	void (*config_changed)(FuPlugin *self);
	/*< private >*/
	gpointer padding[19];
};

/**
 * FuPluginVerifyFlags:
 * @FU_PLUGIN_VERIFY_FLAG_NONE:		No flags set
 *
 * Flags used when verifying, currently unused.
 **/
typedef enum {
	FU_PLUGIN_VERIFY_FLAG_NONE = 0,
	/*< private >*/
	FU_PLUGIN_VERIFY_FLAG_LAST
} FuPluginVerifyFlags;

/**
 * FuPluginVfuncs:
 *
 * The virtual functions that are implemented by the plugins.
 **/
typedef struct {
	/**
	 * build_hash:
	 *
	 * Sets the plugin build hash which must be set to avoid tainting the engine.
	 *
	 * Since: 1.7.2
	 **/
	const gchar *build_hash;
	/**
	 * init:
	 * @self: A #FuPlugin
	 *
	 * Initializes the plugin.
	 * Sets up any static data structures for the plugin.
	 * Most plugins should call fu_plugin_set_build_hash in here.
	 *
	 * Since: 1.7.2
	 **/
	void (*init)(FuPlugin *self);
	/**
	 * destroy:
	 * @self: a plugin
	 *
	 * Destroys the plugin.
	 * Any allocated memory should be freed here.
	 *
	 * Since: 1.7.2
	 **/
	void (*destroy)(FuPlugin *self);
	/**
	 * startup:
	 * @self: a #FuPlugin
	 * @progress: a #FuProgress
	 * @error: (nullable): optional return location for an error
	 *
	 * Tries to start the plugin.
	 * Returns: TRUE for success or FALSE for failure.
	 *
	 * Any plugins not intended for the system or that have failure communicating
	 * with the device should return FALSE.
	 * Any allocated memory should be freed here.
	 *
	 * Since: 1.7.2
	 **/
	gboolean (*startup)(FuPlugin *self, FuProgress *progress, GError **error);
	/**
	 * coldplug:
	 * @self: a #FuPlugin
	 * @progress: a #FuProgress
	 * @error: (nullable): optional return location for an error
	 *
	 * Probes for devices.
	 *
	 * Since: 1.7.2
	 **/
	gboolean (*coldplug)(FuPlugin *self, FuProgress *progress, GError **error);
	/**
	 * device_created
	 * @self: a #FuPlugin
	 * @dev: a device
	 * @error: (nullable): optional return location for an error
	 *
	 * Function run when the subclassed device has been created.
	 *
	 * Since: 1.7.2
	 **/
	gboolean (*device_created)(FuPlugin *self, FuDevice *device, GError **error);
	/**
	 * device_registered
	 * @self: a #FuPlugin
	 * @dev: a device
	 *
	 * Function run when device registered from another plugin.
	 *
	 * Since: 1.7.2
	 **/
	void (*device_registered)(FuPlugin *self, FuDevice *device);
	/**
	 * device_added
	 * @self: a #FuPlugin
	 * @dev: a device
	 *
	 * Function run when the subclassed device has been added.
	 *
	 * Since: 1.7.2
	 **/
	void (*device_added)(FuPlugin *self, FuDevice *device);
	/**
	 * verify:
	 * @self: a #FuPlugin
	 * @dev: a device
	 * @progress: a #FuProgress
	 * @flags: verify flags
	 * @error: (nullable): optional return location for an error
	 *
	 * Verifies the firmware on the device matches the value stored in the database
	 *
	 * Since: 1.7.2
	 **/
	gboolean (*verify)(FuPlugin *self,
			   FuDevice *device,
			   FuProgress *progress,
			   FuPluginVerifyFlags flags,
			   GError **error);
	/**
	 * get_results:
	 * @self: a #FuPlugin
	 * @dev: a device
	 * @error: (nullable): optional return location for an error
	 *
	 * Obtains historical update results for the device.
	 *
	 * Since: 1.7.2
	 **/
	gboolean (*get_results)(FuPlugin *self, FuDevice *device, GError **error);
	/**
	 * clear_results:
	 * @self: a #FuPlugin
	 * @dev: a device
	 * @error: (nullable): optional return location for an error
	 *
	 * Clears stored update results for the device.
	 *
	 * Since: 1.7.2
	 **/
	gboolean (*clear_results)(FuPlugin *self, FuDevice *device, GError **error);
	/**
	 * backend_device_added
	 * @self: a #FuPlugin
	 * @device: a device
	 * @error: (nullable): optional return location for an error
	 *
	 * Function to run after a device is added by a backend, e.g. by USB or Udev.
	 *
	 * Since: 1.7.2
	 **/
	gboolean (*backend_device_added)(FuPlugin *self, FuDevice *device, GError **error);
	/**
	 * backend_device_changed
	 * @self: a #FuPlugin
	 * @device: a device
	 * @error: (nullable): optional return location for an error
	 *
	 * Function run when the device changed.
	 *
	 * Since: 1.7.2
	 **/
	gboolean (*backend_device_changed)(FuPlugin *self, FuDevice *device, GError **error);
	/**
	 * backend_device_removed
	 * @self: a #FuPlugin
	 * @device: a device
	 * @error: (nullable): optional return location for an error
	 *
	 * Function to run when device is physically removed.
	 *
	 * Since: 1.7.2
	 **/
	gboolean (*backend_device_removed)(FuPlugin *self, FuDevice *device, GError **error);
	/**
	 * add_security_attrs
	 * @self: a #FuPlugin
	 * @attrs: a security attribute
	 *
	 * Function that asks plugins to add Host Security Attributes.
	 *
	 * Since: 1.7.2
	 **/
	void (*add_security_attrs)(FuPlugin *self, FuSecurityAttrs *attrs);
	/**
	 * write_firmware:
	 * @self: a #FuPlugin
	 * @dev: a device
	 * @blob_fw: a data blob
	 * @progress: a #FuProgress
	 * @flags: install flags
	 * @error: (nullable): optional return location for an error
	 *
	 * Updates the firmware on the device with blob_fw
	 *
	 * Since: 1.7.2
	 **/
	gboolean (*write_firmware)(FuPlugin *self,
				   FuDevice *device,
				   GBytes *blob_fw,
				   FuProgress *progress,
				   FwupdInstallFlags flags,
				   GError **error);
	/**
	 * unlock:
	 * @self: a #FuPlugin
	 * @dev: a device
	 * @error: (nullable): optional return location for an error
	 *
	 * Unlocks the device for writes.
	 *
	 * Since: 1.7.2
	 **/
	gboolean (*unlock)(FuPlugin *self, FuDevice *device, GError **error);
	/**
	 * activate:
	 * @self: a #FuPlugin
	 * @dev: a device
	 * @error: (nullable): optional return location for an error
	 *
	 * Activates the new firmware on the device.
	 *
	 * This is intended for devices that it is not safe to immediately activate
	 * the firmware.  It may be called at a more convenient time instead.
	 *
	 * Since: 1.7.2
	 **/
	gboolean (*activate)(FuPlugin *self,
			     FuDevice *device,
			     FuProgress *progress,
			     GError **error);
	/**
	 * attach:
	 * @self: a #FuPlugin
	 * @dev: a device
	 * @error: (nullable): optional return location for an error
	 *
	 * Swaps the device from bootloader mode to runtime mode.
	 *
	 * Since: 1.7.2
	 **/
	gboolean (*attach)(FuPlugin *self, FuDevice *device, FuProgress *progress, GError **error);
	/**
	 * detach:
	 * @self: a #FuPlugin
	 * @dev: a device
	 * @error: (nullable): optional return location for an error
	 *
	 * Swaps the device from runtime mode to bootloader mode.
	 *
	 * Since: 1.7.2
	 **/
	gboolean (*detach)(FuPlugin *self, FuDevice *device, FuProgress *progress, GError **error);
	/**
	 * prepare:
	 * @self: a #FuPlugin
	 * @device: a device
	 * @progress: a #FuProgress
	 * @flags: install flags
	 * @error: (nullable): optional return location for an error
	 *
	 * Prepares the device to receive an update.
	 *
	 * Since: 1.7.2
	 **/
	gboolean (*prepare)(FuPlugin *self,
			    FuDevice *device,
			    FuProgress *progress,
			    FwupdInstallFlags flags,
			    GError **error);
	/**
	 * cleanup
	 * @self: a #FuPlugin
	 * @device: a device
	 * @progress: a #FuProgress
	 * @flags: install flags
	 * @error: (nullable): optional return location for an error
	 *
	 * Cleans up the device after receiving an update.
	 *
	 * Since: 1.7.2
	 **/
	gboolean (*cleanup)(FuPlugin *self,
			    FuDevice *device,
			    FuProgress *progress,
			    FwupdInstallFlags flags,
			    GError **error);
	/**
	 * composite_prepare
	 * @self: a #FuPlugin
	 * @devices: (element-type FuDevice): array of devices
	 * @error: (nullable): optional return location for an error
	 *
	 * Function run before updating group of composite devices.
	 *
	 * Since: 1.7.2
	 **/
	gboolean (*composite_prepare)(FuPlugin *self, GPtrArray *devices, GError **error);
	/**
	 * composite_cleanup
	 * @self: a #FuPlugin
	 * @devices: (element-type FuDevice): array of devices
	 * @error: (nullable): optional return location for an error
	 *
	 * Function run after updating group of composite devices.
	 *
	 * Since: 1.7.2
	 **/
	gboolean (*composite_cleanup)(FuPlugin *self, GPtrArray *devices, GError **error);
	/**
	 * load
	 * @ctx: a #FuContext
	 *
	 * Function to register context attributes, run during early startup even on plugins which
	 * will be later disabled.
	 *
	 * Since: 1.8.1
	 **/
	void (*load)(FuContext *ctx);
	/*< private >*/
	gpointer padding[8];
} FuPluginVfuncs;

/**
 * FuPluginRule:
 * @FU_PLUGIN_RULE_CONFLICTS:		The plugin conflicts with another
 * @FU_PLUGIN_RULE_RUN_AFTER:		Order the plugin after another
 * @FU_PLUGIN_RULE_RUN_BEFORE:		Order the plugin before another
 * @FU_PLUGIN_RULE_BETTER_THAN:		Is better than another plugin
 * @FU_PLUGIN_RULE_INHIBITS_IDLE:	The plugin inhibits the idle shutdown
 * @FU_PLUGIN_RULE_METADATA_SOURCE:	Uses another plugin as a source of report metadata
 *
 * The rules used for ordering plugins.
 * Plugins are expected to add rules in fu_plugin_initialize().
 **/
typedef enum {
	FU_PLUGIN_RULE_CONFLICTS,
	FU_PLUGIN_RULE_RUN_AFTER,
	FU_PLUGIN_RULE_RUN_BEFORE,
	FU_PLUGIN_RULE_BETTER_THAN,
	FU_PLUGIN_RULE_INHIBITS_IDLE,
	FU_PLUGIN_RULE_METADATA_SOURCE, /* Since: 1.3.6 */
	/*< private >*/
	FU_PLUGIN_RULE_LAST
} FuPluginRule;

/**
 * FuPluginData:
 *
 * The plugin-allocated private data.
 **/
typedef struct FuPluginData FuPluginData;

/* for plugins to use */
const gchar *
fu_plugin_get_name(FuPlugin *self);
FuPluginData *
fu_plugin_get_data(FuPlugin *self);
FuPluginData *
fu_plugin_alloc_data(FuPlugin *self, gsize data_sz);
FuContext *
fu_plugin_get_context(FuPlugin *self);
void
fu_plugin_device_add(FuPlugin *self, FuDevice *device);
void
fu_plugin_device_remove(FuPlugin *self, FuDevice *device);
void
fu_plugin_device_register(FuPlugin *self, FuDevice *device);
void
fu_plugin_add_device_gtype(FuPlugin *self, GType device_gtype);
void
fu_plugin_add_firmware_gtype(FuPlugin *self, const gchar *id, GType gtype);
void
fu_plugin_add_udev_subsystem(FuPlugin *self, const gchar *subsystem);
gpointer
fu_plugin_cache_lookup(FuPlugin *self, const gchar *id);
void
fu_plugin_cache_remove(FuPlugin *self, const gchar *id);
void
fu_plugin_cache_add(FuPlugin *self, const gchar *id, gpointer dev);
GPtrArray *
fu_plugin_get_devices(FuPlugin *self);
void
fu_plugin_add_rule(FuPlugin *self, FuPluginRule rule, const gchar *name);
void
fu_plugin_add_report_metadata(FuPlugin *self, const gchar *key, const gchar *value);
gchar *
fu_plugin_get_config_value(FuPlugin *self, const gchar *key);
gboolean
fu_plugin_set_secure_config_value(FuPlugin *self,
				  const gchar *key,
				  const gchar *value,
				  GError **error);
gboolean
fu_plugin_get_config_value_boolean(FuPlugin *self, const gchar *key);
gboolean
fu_plugin_set_config_value(FuPlugin *self, const gchar *key, const gchar *value, GError **error);
