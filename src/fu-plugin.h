/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2015 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#ifndef __FU_PLUGIN_H
#define __FU_PLUGIN_H

#include <glib-object.h>
#include <gio/gio.h>
#include <glib-object.h>
#include <gusb.h>

#include "fu-common.h"
#include "fu-device.h"
#include "fu-device-locker.h"
#include "fu-quirks.h"
#include "fu-hwids.h"
#include "fu-usb-device.h"

G_BEGIN_DECLS

#define FU_TYPE_PLUGIN (fu_plugin_get_type ())
G_DECLARE_DERIVABLE_TYPE (FuPlugin, fu_plugin, FU, PLUGIN, GObject)

struct _FuPluginClass
{
	GObjectClass	 parent_class;
	/* signals */
	void		 (* device_added)		(FuPlugin	*plugin,
							 FuDevice	*device);
	void		 (* device_removed)		(FuPlugin	*plugin,
							 FuDevice	*device);
	void		 (* status_changed)		(FuPlugin	*plugin,
							 FwupdStatus	 status);
	void		 (* percentage_changed)		(FuPlugin	*plugin,
							 guint		 percentage);
	void		 (* recoldplug)			(FuPlugin	*plugin);
	void		 (* set_coldplug_delay)		(FuPlugin	*plugin,
							 guint		 duration);
	void		 (* device_register)		(FuPlugin	*plugin,
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
 *
 * The rules used for ordering plugins.
 * Plugins are expected to add rules in fu_plugin_initialize().
 **/
typedef enum {
	FU_PLUGIN_RULE_CONFLICTS,
	FU_PLUGIN_RULE_RUN_AFTER,
	FU_PLUGIN_RULE_RUN_BEFORE,
	/*< private >*/
	FU_PLUGIN_RULE_LAST
} FuPluginRule;

typedef struct	FuPluginData	FuPluginData;

/* for plugins to use */
const gchar	*fu_plugin_get_name			(FuPlugin	*plugin);
FuPluginData	*fu_plugin_get_data			(FuPlugin	*plugin);
FuPluginData	*fu_plugin_alloc_data			(FuPlugin	*plugin,
							 gsize		 data_sz);
gboolean	 fu_plugin_get_enabled			(FuPlugin	*plugin);
void		 fu_plugin_set_enabled			(FuPlugin	*plugin,
							 gboolean	 enabled);
GUsbContext	*fu_plugin_get_usb_context		(FuPlugin	*plugin);
GPtrArray	*fu_plugin_get_supported		(FuPlugin	*plugin);
void		 fu_plugin_device_add			(FuPlugin	*plugin,
							 FuDevice	*device);
void		 fu_plugin_device_add_delay		(FuPlugin	*plugin,
							 FuDevice	*device);
void		 fu_plugin_device_remove		(FuPlugin	*plugin,
							 FuDevice	*device);
void		 fu_plugin_device_register		(FuPlugin	*plugin,
							 FuDevice	*device);
void		 fu_plugin_set_status			(FuPlugin	*plugin,
							 FwupdStatus	 status);
void		 fu_plugin_set_percentage		(FuPlugin	*plugin,
							 guint		 percentage);
void		 fu_plugin_request_recoldplug		(FuPlugin	*plugin);
void		 fu_plugin_set_coldplug_delay		(FuPlugin	*plugin,
							 guint		 duration);
gpointer	 fu_plugin_cache_lookup			(FuPlugin	*plugin,
							 const gchar	*id);
void		 fu_plugin_cache_remove			(FuPlugin	*plugin,
							 const gchar	*id);
void		 fu_plugin_cache_add			(FuPlugin	*plugin,
							 const gchar	*id,
							 gpointer	 dev);
gboolean	 fu_plugin_check_hwid			(FuPlugin	*plugin,
							 const gchar	*hwid);
gboolean	 fu_plugin_check_supported		(FuPlugin	*plugin,
							 const gchar	*guid);
const gchar	*fu_plugin_get_dmi_value		(FuPlugin	*plugin,
							 const gchar	*dmi_id);
const gchar	*fu_plugin_get_smbios_string		(FuPlugin	*plugin,
							 guint8		 structure_type,
							 guint8		 offset);
GBytes		*fu_plugin_get_smbios_data		(FuPlugin	*plugin,
							 guint8		 structure_type);
void		 fu_plugin_add_rule			(FuPlugin	*plugin,
							 FuPluginRule	 rule,
							 const gchar	*name);
FuQuirks	*fu_plugin_get_quirks			(FuPlugin	*plugin);
const gchar	*fu_plugin_lookup_quirk_by_id		(FuPlugin	*plugin,
							 const gchar	*prefix,
							 const gchar	*id);
const gchar	*fu_plugin_lookup_quirk_by_usb_device	(FuPlugin	*plugin,
							 const gchar	*prefix,
							 GUsbDevice	*dev);
void		 fu_plugin_add_report_metadata		(FuPlugin	*plugin,
							 const gchar	*key,
							 const gchar	*value);
gchar		*fu_plugin_get_config_value		(FuPlugin	*plugin,
							 const gchar	*key);
void		 fu_plugin_add_runtime_version		(FuPlugin	*plugin,
							 const gchar	*component_id,
							 const gchar	*version);
void		 fu_plugin_add_compile_version		(FuPlugin	*plugin,
							 const gchar	*component_id,
							 const gchar	*version);

G_END_DECLS

#endif /* __FU_PLUGIN_H */

