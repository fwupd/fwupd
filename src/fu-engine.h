/*
 * Copyright (C) 2017 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#pragma once

#include <xmlb.h>
#include <glib-object.h>

#include "fwupd-device.h"
#include "fwupd-enums.h"

#include "fu-common.h"
#include "fu-keyring.h"
#include "fu-install-task.h"
#include "fu-plugin.h"

#define FU_TYPE_ENGINE (fu_engine_get_type ())
G_DECLARE_FINAL_TYPE (FuEngine, fu_engine, FU, ENGINE, GObject)

/**
 * FuEngineLoadFlags:
 * @FU_ENGINE_LOAD_FLAG_NONE:		No flags set
 * @FU_ENGINE_LOAD_FLAG_READONLY_FS:	Ignore readonly filesystem errors
 *
 * The flags to use when loading the engine.
 **/
typedef enum {
	FU_ENGINE_LOAD_FLAG_NONE		= 0,
	FU_ENGINE_LOAD_FLAG_READONLY_FS		= 1 << 0,
	FU_ENGINE_LOAD_FLAG_NO_ENUMERATE	= 1 << 1,
	/*< private >*/
	FU_ENGINE_LOAD_FLAG_LAST
} FuEngineLoadFlags;

FuEngine	*fu_engine_new				(FuAppFlags	 app_flags);
void		 fu_engine_add_app_flag			(FuEngine	*self,
							 FuAppFlags	 app_flags);
void		 fu_engine_add_plugin_filter		(FuEngine	*self,
							 const gchar	*plugin_glob);
void		 fu_engine_idle_reset			(FuEngine	*self);
gboolean	 fu_engine_load				(FuEngine	*self,
							 FuEngineLoadFlags flags,
							 GError		**error);
gboolean	 fu_engine_load_plugins			(FuEngine	*self,
							 GError		**error);
gboolean	 fu_engine_get_tainted			(FuEngine	*self);
const gchar	*fu_engine_get_host_product		(FuEngine *self);
const gchar	*fu_engine_get_host_machine_id		(FuEngine *self);
FwupdStatus	 fu_engine_get_status			(FuEngine	*self);
XbSilo		*fu_engine_get_silo_from_blob		(FuEngine	*self,
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
FwupdRemote 	*fu_engine_get_remote_by_id		(FuEngine	*self,
							 const gchar	*remote_id,
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
gboolean	 fu_engine_update_metadata_bytes	(FuEngine	*self,
							 const gchar	*remote_id,
							 GBytes		*bytes_raw,
							 GBytes		*bytes_sig,
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
GBytes		*fu_engine_firmware_read		(FuEngine	*self,
							 FuDevice	*device,
							 FwupdInstallFlags flags,
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
gboolean	 fu_engine_composite_prepare		(FuEngine	*self,
							 GPtrArray	*devices,
							 GError		**error);
gboolean	 fu_engine_composite_cleanup		(FuEngine	*self,
							 GPtrArray	*devices,
							 GError		**error);
gboolean	 fu_engine_install			(FuEngine	*self,
							 FuInstallTask	*task,
							 GBytes		*blob_cab,
							 FwupdInstallFlags flags,
							 GError		**error);
gboolean	 fu_engine_install_blob			(FuEngine	*self,
							 FuDevice	*device,
							 GBytes		*blob_fw,
							 FwupdInstallFlags flags,
							 GError		**error);
gboolean	 fu_engine_install_tasks		(FuEngine	*self,
							 GPtrArray	*install_tasks,
							 GBytes		*blob_cab,
							 FwupdInstallFlags flags,
							 GError		**error);
GPtrArray	*fu_engine_get_details			(FuEngine	*self,
							 gint		 fd,
							 GError		**error);
gboolean	 fu_engine_activate			(FuEngine	*self,
							 const gchar	*device_id,
							 GError		**error);
GPtrArray	*fu_engine_get_approved_firmware	(FuEngine	*self);
void		 fu_engine_add_approved_firmware	(FuEngine	*self,
							 const gchar	*checksum);
gchar		*fu_engine_self_sign			(FuEngine	*self,
							 const gchar	*value,
							 FuKeyringSignFlags flags,
							 GError		**error);
gboolean	 fu_engine_modify_config		(FuEngine	*self,
							 const gchar	*key,
							 const gchar	*value,
							 GError		**error);
GPtrArray	*fu_engine_get_firmware_gtype_ids	(FuEngine	*engine);
GType		 fu_engine_get_firmware_gtype_by_id	(FuEngine	*engine,
							 const gchar	*id);

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
void		 fu_engine_set_silo			(FuEngine	*self,
							 XbSilo		*silo);
XbNode		*fu_engine_get_component_by_guids	(FuEngine	*self,
							 FuDevice	*device);
gboolean	 fu_engine_schedule_update		(FuEngine	*self,
							 FuDevice	*device,
							 FwupdRelease	*release,
							 GBytes		*blob_cab,
							 FwupdInstallFlags flags,
							 GError		**error);
