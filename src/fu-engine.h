/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2017 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#ifndef __FU_ENGINE_H
#define __FU_ENGINE_H

G_BEGIN_DECLS

#include <appstream-glib.h>
#include <glib-object.h>

#include "fwupd-device.h"
#include "fwupd-enums.h"

#include "fu-common.h"
#include "fu-install-task.h"
#include "fu-plugin.h"

#define FU_TYPE_ENGINE (fu_engine_get_type ())
G_DECLARE_FINAL_TYPE (FuEngine, fu_engine, FU, ENGINE, GObject)

FuEngine	*fu_engine_new				(FuAppFlags	 app_flags);
void		 fu_engine_add_plugin_filter		(FuEngine	*self,
							 const gchar	*plugin_glob);
gboolean	 fu_engine_load				(FuEngine	*self,
							 GError		**error);
gboolean	 fu_engine_load_plugins			(FuEngine	*self,
							 GError		**error);
FwupdStatus	 fu_engine_get_status			(FuEngine	*self);
void		 fu_engine_profile_dump			(FuEngine	*self);
gboolean	 fu_engine_check_plugins_pending	(FuEngine	*self,
							 GError		**error);
AsStore		*fu_engine_get_store_from_blob		(FuEngine	*self,
							 GBytes		*blob_cab,
							 GError		**error);
guint64		 fu_engine_get_archive_size_max		(FuEngine	*self);
GPtrArray	*fu_engine_get_plugins			(FuEngine	*self);
GPtrArray	*fu_engine_get_devices			(FuEngine	*self,
							 GError		**error);
FuDevice	*fu_engine_get_device			(FuEngine	*self,
							 const gchar	*device_id,
							 GError		**error);
GPtrArray	*fu_engine_get_history			(FuEngine	*self,
							 GError		**error);
GPtrArray	*fu_engine_get_remotes			(FuEngine	*self,
							 GError		**error);
GPtrArray	*fu_engine_get_releases			(FuEngine	*self,
							 const gchar	*device_id,
							 GError		**error);
GPtrArray	*fu_engine_get_downgrades		(FuEngine	*self,
							 const gchar	*device_id,
							 GError		**error);
GPtrArray	*fu_engine_get_upgrades			(FuEngine	*self,
							 const gchar	*device_id,
							 GError		**error);
FwupdDevice	*fu_engine_get_results			(FuEngine	*self,
							 const gchar	*device_id,
							 GError		**error);
gboolean	 fu_engine_clear_results		(FuEngine	*self,
							 const gchar	*device_id,
							 GError		**error);
gboolean	 fu_engine_update_metadata		(FuEngine	*self,
							 const gchar	*remote_id,
							 gint		 fd,
							 gint		 fd_sig,
							 GError		**error);
gboolean	 fu_engine_unlock			(FuEngine	*self,
							 const gchar	*device_id,
							 GError		**error);
gboolean	 fu_engine_verify			(FuEngine	*self,
							 const gchar	*device_id,
							 GError		**error);
gboolean	 fu_engine_verify_update		(FuEngine	*self,
							 const gchar	*device_id,
							 GError		**error);
gboolean	 fu_engine_modify_remote		(FuEngine	*self,
							 const gchar	*remote_id,
							 const gchar	*key,
							 const gchar	*value,
							 GError		**error);
gboolean	 fu_engine_modify_device		(FuEngine	*self,
							 const gchar	*device_id,
							 const gchar	*key,
							 const gchar	*value,
							 GError		**error);
gboolean	 fu_engine_install			(FuEngine	*self,
							 FuInstallTask	*task,
							 GBytes		*blob_cab,
							 FwupdInstallFlags flags,
							 GError		**error);
gboolean	 fu_engine_install_blob			(FuEngine	*self,
							 FuDevice	*device,
							 GBytes		*blob_cab,
							 GBytes		*blob_fw,
							 const gchar	*version,
							 FwupdInstallFlags flags,
							 GError		**error);
GPtrArray	*fu_engine_get_details			(FuEngine	*self,
							 gint		 fd,
							 GError		**error);

/* for the self tests */
void		 fu_engine_add_device			(FuEngine	*self,
							 FuDevice	*device);
void		 fu_engine_add_plugin			(FuEngine	*self,
							 FuPlugin	*plugin);
void		 fu_engine_add_runtime_version		(FuEngine	*self,
							 const gchar	*component_id,
							 const gchar	*version);
gboolean	 fu_engine_check_requirements		(FuEngine	*self,
							 FuInstallTask	*task,
							 FwupdInstallFlags flags,
							 GError		**error);

G_END_DECLS

#endif /* __FU_ENGINE_H */

