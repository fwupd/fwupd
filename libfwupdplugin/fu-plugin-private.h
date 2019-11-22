/*
 * Copyright (C) 2016-2018 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#pragma once

#include "fu-quirks.h"
#include "fu-plugin.h"
#include "fu-smbios.h"

FuPlugin	*fu_plugin_new				(void);
gboolean	 fu_plugin_is_open			(FuPlugin	*self);
void		 fu_plugin_set_usb_context		(FuPlugin	*self,
							 GUsbContext	*usb_ctx);
void		 fu_plugin_set_hwids			(FuPlugin	*self,
							 FuHwids	*hwids);
void		 fu_plugin_set_udev_subsystems		(FuPlugin	*self,
							 GPtrArray	*udev_subsystems);
void		 fu_plugin_set_quirks			(FuPlugin	*self,
							 FuQuirks	*quirks);
void		 fu_plugin_set_runtime_versions		(FuPlugin	*self,
							 GHashTable	*runtime_versions);
void		 fu_plugin_set_compile_versions		(FuPlugin	*self,
							 GHashTable	*compile_versions);
void		 fu_plugin_set_smbios			(FuPlugin	*self,
							 FuSmbios	*smbios);
guint		 fu_plugin_get_order			(FuPlugin	*self);
void		 fu_plugin_set_order			(FuPlugin	*self,
							 guint		 order);
guint		 fu_plugin_get_priority			(FuPlugin	*self);
void		 fu_plugin_set_priority			(FuPlugin	*self,
							 guint		 priority);
void		 fu_plugin_set_name			(FuPlugin	*self,
							 const gchar 	*name);
const gchar	*fu_plugin_get_build_hash		(FuPlugin	*self);
GPtrArray	*fu_plugin_get_rules			(FuPlugin	*self,
							 FuPluginRule	 rule);
gboolean	 fu_plugin_has_rule			(FuPlugin	*self,
							 FuPluginRule	 rule,
							 const gchar	*name);
GHashTable	*fu_plugin_get_report_metadata		(FuPlugin	*self);
gboolean	 fu_plugin_open				(FuPlugin	*self,
							 const gchar	*filename,
							 GError		**error);
gboolean	 fu_plugin_runner_startup		(FuPlugin	*self,
							 GError		**error);
gboolean	 fu_plugin_runner_coldplug		(FuPlugin	*self,
							 GError		**error);
gboolean	 fu_plugin_runner_coldplug_prepare	(FuPlugin	*self,
							 GError		**error);
gboolean	 fu_plugin_runner_coldplug_cleanup	(FuPlugin	*self,
							 GError		**error);
gboolean	 fu_plugin_runner_recoldplug		(FuPlugin	*self,
							 GError		**error);
gboolean	 fu_plugin_runner_update_prepare	(FuPlugin	*self,
							 FwupdInstallFlags flags,
							 FuDevice	*device,
							 GError		**error);
gboolean	 fu_plugin_runner_update_cleanup	(FuPlugin	*self,
							 FwupdInstallFlags flags,
							 FuDevice	*device,
							 GError		**error);
gboolean	 fu_plugin_runner_composite_prepare	(FuPlugin	*self,
							 GPtrArray	*devices,
							 GError		**error);
gboolean	 fu_plugin_runner_composite_cleanup	(FuPlugin	*self,
							 GPtrArray	*devices,
							 GError		**error);
gboolean	 fu_plugin_runner_update_attach		(FuPlugin	*self,
							 FuDevice	*device,
							 GError		**error);
gboolean	 fu_plugin_runner_update_detach		(FuPlugin	*self,
							 FuDevice	*device,
							 GError		**error);
gboolean	 fu_plugin_runner_update_reload		(FuPlugin	*self,
							 FuDevice	*device,
							 GError		**error);
gboolean	 fu_plugin_runner_usb_device_added	(FuPlugin	*self,
							 FuUsbDevice	*device,
							 GError		**error);
gboolean	 fu_plugin_runner_udev_device_added	(FuPlugin	*self,
							 FuUdevDevice	*device,
							 GError		**error);
gboolean	 fu_plugin_runner_udev_device_changed	(FuPlugin	*self,
							 FuUdevDevice	*device,
							 GError		**error);
void		 fu_plugin_runner_device_removed	(FuPlugin	*self,
							 FuDevice	*device);
void		 fu_plugin_runner_device_register	(FuPlugin	*self,
							 FuDevice	*device);
gboolean	 fu_plugin_runner_update		(FuPlugin	*self,
							 FuDevice	*device,
							 GBytes		*blob_fw,
							 FwupdInstallFlags flags,
							 GError		**error);
gboolean	 fu_plugin_runner_verify		(FuPlugin	*self,
							 FuDevice	*device,
							 FuPluginVerifyFlags flags,
							 GError		**error);
gboolean	 fu_plugin_runner_activate 		(FuPlugin *self,
							 FuDevice *device,
							 GError **error);
gboolean	 fu_plugin_runner_unlock		(FuPlugin	*self,
							 FuDevice	*device,
							 GError		**error);
gboolean	 fu_plugin_runner_clear_results		(FuPlugin	*self,
							 FuDevice	*device,
							 GError		**error);
gboolean	 fu_plugin_runner_get_results		(FuPlugin	*self,
							 FuDevice	*device,
							 GError		**error);
gint		 fu_plugin_name_compare			(FuPlugin	*plugin1,
							 FuPlugin	*plugin2);
gint		 fu_plugin_order_compare		(FuPlugin	*plugin1,
							 FuPlugin	*plugin2);

/* utils */
gchar		*fu_plugin_guess_name_from_fn           (const gchar	*filename);
