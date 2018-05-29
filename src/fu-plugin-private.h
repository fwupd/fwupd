/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2016-2018 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#ifndef __FU_PLUGIN_PRIVATE_H
#define __FU_PLUGIN_PRIVATE_H

#include "fu-quirks.h"
#include "fu-plugin.h"
#include "fu-smbios.h"

G_BEGIN_DECLS

#define FU_OFFLINE_TRIGGER_FILENAME	FU_OFFLINE_DESTDIR "/system-update"

FuPlugin	*fu_plugin_new				(void);
gboolean	 fu_plugin_has_device_delay		(FuPlugin	*plugin);
void		 fu_plugin_set_usb_context		(FuPlugin	*plugin,
							 GUsbContext	*usb_ctx);
void		 fu_plugin_set_hwids			(FuPlugin	*plugin,
							 FuHwids	*hwids);
void		 fu_plugin_set_supported		(FuPlugin	*plugin,
							 GPtrArray	*supported_guids);
void		 fu_plugin_set_quirks			(FuPlugin	*plugin,
							 FuQuirks	*quirks);
void		 fu_plugin_set_runtime_versions		(FuPlugin	*plugin,
							 GHashTable	*runtime_versions);
void		 fu_plugin_set_compile_versions		(FuPlugin	*plugin,
							 GHashTable	*compile_versions);
void		 fu_plugin_set_smbios			(FuPlugin	*plugin,
							 FuSmbios	*smbios);
guint		 fu_plugin_get_order			(FuPlugin	*plugin);
void		 fu_plugin_set_order			(FuPlugin	*plugin,
							 guint		 order);
void		 fu_plugin_set_name			(FuPlugin	*plugin,
							 const gchar 	*name);
GPtrArray	*fu_plugin_get_rules			(FuPlugin	*plugin,
							 FuPluginRule	 rule);
GHashTable	*fu_plugin_get_report_metadata		(FuPlugin	*plugin);
gboolean	 fu_plugin_open				(FuPlugin	*plugin,
							 const gchar	*filename,
							 GError		**error);
gboolean	 fu_plugin_runner_startup		(FuPlugin	*plugin,
							 GError		**error);
gboolean	 fu_plugin_runner_coldplug		(FuPlugin	*plugin,
							 GError		**error);
gboolean	 fu_plugin_runner_coldplug_prepare	(FuPlugin	*plugin,
							 GError		**error);
gboolean	 fu_plugin_runner_coldplug_cleanup	(FuPlugin	*plugin,
							 GError		**error);
gboolean	 fu_plugin_runner_recoldplug		(FuPlugin	*plugin,
							 GError		**error);
gboolean	 fu_plugin_runner_update_prepare	(FuPlugin	*plugin,
							 FuDevice	*device,
							 GError		**error);
gboolean	 fu_plugin_runner_update_cleanup	(FuPlugin	*plugin,
							 FuDevice	*device,
							 GError		**error);
gboolean	 fu_plugin_runner_update_attach		(FuPlugin	*plugin,
							 FuDevice	*device,
							 GError		**error);
gboolean	 fu_plugin_runner_update_detach		(FuPlugin	*plugin,
							 FuDevice	*device,
							 GError		**error);
gboolean	 fu_plugin_runner_update_reload		(FuPlugin	*plugin,
							 FuDevice	*device,
							 GError		**error);
gboolean	 fu_plugin_runner_usb_device_added	(FuPlugin	*plugin,
							 GUsbDevice	*usb_device,
							 GError		**error);
void		 fu_plugin_runner_device_register	(FuPlugin	*plugin,
							 FuDevice	*device);
gboolean	 fu_plugin_runner_update		(FuPlugin	*plugin,
							 FuDevice	*device,
							 GBytes		*blob_cab,
							 GBytes		*blob_fw,
							 FwupdInstallFlags flags,
							 GError		**error);
gboolean	 fu_plugin_runner_verify		(FuPlugin	*plugin,
							 FuDevice	*device,
							 FuPluginVerifyFlags flags,
							 GError		**error);
gboolean	 fu_plugin_runner_unlock		(FuPlugin	*plugin,
							 FuDevice	*device,
							 GError		**error);
gboolean	 fu_plugin_runner_clear_results		(FuPlugin	*plugin,
							 FuDevice	*device,
							 GError		**error);
gboolean	 fu_plugin_runner_get_results		(FuPlugin	*plugin,
							 FuDevice	*device,
							 GError		**error);
gint		 fu_plugin_name_compare			(FuPlugin	*plugin1,
							 FuPlugin	*plugin2);
gint		 fu_plugin_order_compare		(FuPlugin	*plugin1,
							 FuPlugin	*plugin2);

/* utils */
gchar		*fu_plugin_guess_name_from_fn           (const gchar	*filename);

G_END_DECLS

#endif /* __FU_PLUGIN_PRIVATE_H */
