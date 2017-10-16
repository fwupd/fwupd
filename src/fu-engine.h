/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2017 Richard Hughes <richard@hughsie.com>
 *
 * Licensed under the GNU Lesser General Public License Version 2.1
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301 USA
 */

#ifndef __FU_ENGINE_H
#define __FU_ENGINE_H

G_BEGIN_DECLS

#include <appstream-glib.h>
#include <glib-object.h>

#include "fwupd-device.h"
#include "fwupd-enums.h"

#include "fu-plugin.h"

#define FU_TYPE_ENGINE (fu_engine_get_type ())
G_DECLARE_FINAL_TYPE (FuEngine, fu_engine, FU, ENGINE, GObject)

#define		FU_ENGINE_FIRMWARE_SIZE_MAX		(32 * 1024 * 1024) /* bytes */

FuEngine	*fu_engine_new				(void);
gboolean	 fu_engine_load				(FuEngine	*self,
							 GError		**error);
FwupdStatus	 fu_engine_get_status			(FuEngine	*self);
void		 fu_engine_profile_dump			(FuEngine	*self);
gboolean	 fu_engine_check_plugins_pending	(FuEngine	*self,
							 GError		**error);
AsStore		*fu_engine_get_store_from_blob		(FuEngine	*self,
							 GBytes		*blob_cab,
							 GError		**error);
const gchar	*fu_engine_get_action_id_for_device	(FuEngine	*self,
							 const gchar	*device_id,
							 AsStore	*store,
							 FwupdInstallFlags flags,
							 GError		**error);

GPtrArray	*fu_engine_get_devices			(FuEngine	*self,
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
gboolean	 fu_engine_install			(FuEngine	*self,
							 const gchar	*device_id,
							 AsStore	*store,
							 GBytes		*blob_cab,
							 FwupdInstallFlags flags,
							 GError		**error);
GPtrArray	*fu_engine_get_details			(FuEngine	*self,
							 gint		 fd,
							 GError		**error);

/* for the self tests */
void		 fu_engine_add_device			(FuEngine	*self,
							 FuPlugin	*plugin,
							 FuDevice	*device);

G_END_DECLS

#endif /* __FU_ENGINE_H */

