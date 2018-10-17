/*
 * Copyright (C) 2015 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#ifndef __FU_PLUGIN_H
#define __FU_PLUGIN_H

#include <gio/gio.h>
#include <gusb.h>
#include <gudev/gudev.h>

#include "fu-common.h"
#include "fu-common-guid.h"
#include "fu-common-version.h"
#include "fu-device.h"
#include "fu-device-locker.h"
#include "fu-quirks.h"
#include "fu-hwids.h"
#include "fu-usb-device.h"
#include "fu-udev-device.h"

G_BEGIN_DECLS

#define FU_TYPE_PLUGIN (fu_plugin_get_type ())
G_DECLARE_DERIVABLE_TYPE (FuPlugin, fu_plugin, FU, PLUGIN, GObject)

struct _FuPluginClass
{
	GObjectClass	 parent_class;
	/* signals */
	void		 (* device_added)		(FuPlugin	*self,
							 FuDevice	*device);
	void		 (* device_removed)		(FuPlugin	*self,
							 FuDevice	*device);
	void		 (* status_changed)		(FuPlugin	*self,
							 FwupdStatus	 status);
	void		 (* percentage_changed)		(FuPlugin	*self,
							 guint		 percentage);
	void		 (* recoldplug)			(FuPlugin	*self);
	void		 (* set_coldplug_delay)		(FuPlugin	*self,
							 guint		 duration);
	void		 (* device_register)		(FuPlugin	*self,
							 FuDevice	*device);
	/*< private >*/
	gpointer	padding[24];
};

/**
 * FuPluginVerifyFlags:
 * @FU_PLUGIN_VERIFY_FLAG_NONE:		No flags set
 *
 * Flags used when verifying, currently unused.
 **/
typedef enum {
	FU_PLUGIN_VERIFY_FLAG_NONE		= 0,
	/*< private >*/
	FU_PLUGIN_VERIFY_FLAG_LAST
} FuPluginVerifyFlags;

/**
 * FuPluginRule:
 * @FU_PLUGIN_RULE_CONFLICTS:		The plugin conflicts with another
 * @FU_PLUGIN_RULE_RUN_AFTER:		Order the plugin after another
 * @FU_PLUGIN_RULE_RUN_BEFORE:		Order the plugin before another
 * @FU_PLUGIN_RULE_REQUIRES_QUIRK:	Requires a specific quirk
 * @FU_PLUGIN_RULE_BETTER_THAN:		Is better than another plugin
 *
 * The rules used for ordering plugins.
 * Plugins are expected to add rules in fu_plugin_initialize().
 **/
typedef enum {
	FU_PLUGIN_RULE_CONFLICTS,
	FU_PLUGIN_RULE_RUN_AFTER,
	FU_PLUGIN_RULE_RUN_BEFORE,
	FU_PLUGIN_RULE_REQUIRES_QUIRK,
	FU_PLUGIN_RULE_BETTER_THAN,
	/*< private >*/
	FU_PLUGIN_RULE_LAST
} FuPluginRule;

typedef struct	FuPluginData	FuPluginData;

/* for plugins to use */
const gchar	*fu_plugin_get_name			(FuPlugin	*self);
FuPluginData	*fu_plugin_get_data			(FuPlugin	*self);
FuPluginData	*fu_plugin_alloc_data			(FuPlugin	*self,
							 gsize		 data_sz);
gboolean	 fu_plugin_get_enabled			(FuPlugin	*self);
void		 fu_plugin_set_enabled			(FuPlugin	*self,
							 gboolean	 enabled);
GUsbContext	*fu_plugin_get_usb_context		(FuPlugin	*self);
GPtrArray	*fu_plugin_get_supported		(FuPlugin	*self);
void		 fu_plugin_device_add			(FuPlugin	*self,
							 FuDevice	*device);
void		 fu_plugin_device_remove		(FuPlugin	*self,
							 FuDevice	*device);
void		 fu_plugin_device_register		(FuPlugin	*self,
							 FuDevice	*device);
void		 fu_plugin_request_recoldplug		(FuPlugin	*self);
void		 fu_plugin_set_coldplug_delay		(FuPlugin	*self,
							 guint		 duration);
gpointer	 fu_plugin_cache_lookup			(FuPlugin	*self,
							 const gchar	*id);
void		 fu_plugin_cache_remove			(FuPlugin	*self,
							 const gchar	*id);
void		 fu_plugin_cache_add			(FuPlugin	*self,
							 const gchar	*id,
							 gpointer	 dev);
gboolean	 fu_plugin_check_hwid			(FuPlugin	*self,
							 const gchar	*hwid);
GPtrArray	*fu_plugin_get_hwids			(FuPlugin	*self);
gboolean	 fu_plugin_check_supported		(FuPlugin	*self,
							 const gchar	*guid);
const gchar	*fu_plugin_get_dmi_value		(FuPlugin	*self,
							 const gchar	*dmi_id);
const gchar	*fu_plugin_get_smbios_string		(FuPlugin	*self,
							 guint8		 structure_type,
							 guint8		 offset);
GBytes		*fu_plugin_get_smbios_data		(FuPlugin	*self,
							 guint8		 structure_type);
void		 fu_plugin_add_rule			(FuPlugin	*self,
							 FuPluginRule	 rule,
							 const gchar	*name);
void		 fu_plugin_add_udev_subsystem		(FuPlugin	*self,
							 const gchar	*subsystem);
FuQuirks	*fu_plugin_get_quirks			(FuPlugin	*self);
const gchar	*fu_plugin_lookup_quirk_by_id		(FuPlugin	*self,
							 const gchar	*group,
							 const gchar	*key);
guint64		 fu_plugin_lookup_quirk_by_id_as_uint64	(FuPlugin	*self,
							 const gchar	*group,
							 const gchar	*key);
void		 fu_plugin_add_report_metadata		(FuPlugin	*self,
							 const gchar	*key,
							 const gchar	*value);
gchar		*fu_plugin_get_config_value		(FuPlugin	*self,
							 const gchar	*key);
void		 fu_plugin_add_runtime_version		(FuPlugin	*self,
							 const gchar	*component_id,
							 const gchar	*version);
void		 fu_plugin_add_compile_version		(FuPlugin	*self,
							 const gchar	*component_id,
							 const gchar	*version);

G_END_DECLS

#endif /* __FU_PLUGIN_H */

