/*
 * Copyright (C) 2017 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#pragma once

#include <fwupdplugin.h>

#include <jcat.h>

#include "fwupd-device.h"
#include "fwupd-enums.h"

#include "fu-engine-config.h"
#include "fu-release.h"

#define FU_TYPE_ENGINE (fu_engine_get_type())
G_DECLARE_FINAL_TYPE(FuEngine, fu_engine, FU, ENGINE, GObject)

/**
 * FuEngineLoadFlags:
 * @FU_ENGINE_LOAD_FLAG_NONE:		No flags set
 * @FU_ENGINE_LOAD_FLAG_READONLY:	Ignore readonly filesystem errors
 * @FU_ENGINE_LOAD_FLAG_COLDPLUG:	Enumerate devices
 * @FU_ENGINE_LOAD_FLAG_REMOTES:	Enumerate remotes
 * @FU_ENGINE_LOAD_FLAG_HWINFO:		Load details about the hardware
 * @FU_ENGINE_LOAD_FLAG_NO_CACHE:	Do not save persistent xmlb silos
 * @FU_ENGINE_LOAD_FLAG_NO_IDLE_SOURCES:Do not load idle sources
 * @FU_ENGINE_LOAD_FLAG_BUILTIN_PLUGINS:	Load built-in plugins
 * @FU_ENGINE_LOAD_FLAG_ENSURE_CLIENT_CERT:	Ensure the client certificate exists
 * @FU_ENGINE_LOAD_FLAG_EXTERNAL_PLUGINS:	Load external dload'ed plugins such as flashrom
 *
 * The flags to use when loading the engine.
 **/
typedef enum {
	FU_ENGINE_LOAD_FLAG_NONE = 0,
	FU_ENGINE_LOAD_FLAG_READONLY = 1 << 0,
	FU_ENGINE_LOAD_FLAG_COLDPLUG = 1 << 1,
	FU_ENGINE_LOAD_FLAG_REMOTES = 1 << 2,
	FU_ENGINE_LOAD_FLAG_HWINFO = 1 << 3,
	FU_ENGINE_LOAD_FLAG_NO_CACHE = 1 << 4,
	FU_ENGINE_LOAD_FLAG_NO_IDLE_SOURCES = 1 << 5,
	FU_ENGINE_LOAD_FLAG_BUILTIN_PLUGINS = 1 << 6,
	FU_ENGINE_LOAD_FLAG_ENSURE_CLIENT_CERT = 1 << 7,
	FU_ENGINE_LOAD_FLAG_EXTERNAL_PLUGINS = 1 << 8,
	/*< private >*/
	FU_ENGINE_LOAD_FLAG_LAST
} FuEngineLoadFlags;

FuEngine *
fu_engine_new(FuContext *ctx);
gboolean
fu_engine_get_loaded(FuEngine *self);
void
fu_engine_add_plugin_filter(FuEngine *self, const gchar *plugin_glob);
void
fu_engine_idle_reset(FuEngine *self);
guint32
fu_engine_idle_inhibit(FuEngine *self, FuIdleInhibit inhibit, const gchar *reason);
void
fu_engine_idle_uninhibit(FuEngine *self, guint32 token);
gboolean
fu_engine_load(FuEngine *self, FuEngineLoadFlags flags, FuProgress *progress, GError **error);
const gchar *
fu_engine_get_host_vendor(FuEngine *self);
const gchar *
fu_engine_get_host_product(FuEngine *self);
const gchar *
fu_engine_get_host_machine_id(FuEngine *self);
const gchar *
fu_engine_get_host_bkc(FuEngine *self);
gboolean
fu_engine_is_uid_trusted(FuEngine *self, guint64 calling_uid);
const gchar *
fu_engine_get_host_security_id(FuEngine *self);
XbSilo *
fu_engine_get_silo_from_blob(FuEngine *self, GBytes *blob_cab, GError **error);
FuEngineConfig *
fu_engine_get_config(FuEngine *self);
GPtrArray *
fu_engine_get_plugins(FuEngine *self);
FuPlugin *
fu_engine_get_plugin_by_name(FuEngine *self, const gchar *name, GError **error);
GPtrArray *
fu_engine_get_devices(FuEngine *self, GError **error);
FuDevice *
fu_engine_get_device(FuEngine *self, const gchar *device_id, GError **error);
GPtrArray *
fu_engine_get_devices_by_guid(FuEngine *self, const gchar *guid, GError **error);
GPtrArray *
fu_engine_get_devices_by_composite_id(FuEngine *self, const gchar *composite_id, GError **error);
GPtrArray *
fu_engine_get_history(FuEngine *self, GError **error);
FwupdRemote *
fu_engine_get_remote_by_id(FuEngine *self, const gchar *remote_id, GError **error);
GPtrArray *
fu_engine_get_remotes(FuEngine *self, GError **error);
GPtrArray *
fu_engine_get_releases(FuEngine *self,
		       FuEngineRequest *request,
		       const gchar *device_id,
		       GError **error);
GPtrArray *
fu_engine_get_downgrades(FuEngine *self,
			 FuEngineRequest *request,
			 const gchar *device_id,
			 GError **error);
GPtrArray *
fu_engine_get_upgrades(FuEngine *self,
		       FuEngineRequest *request,
		       const gchar *device_id,
		       GError **error);
FwupdDevice *
fu_engine_get_results(FuEngine *self, const gchar *device_id, GError **error);
FuSecurityAttrs *
fu_engine_get_host_security_attrs(FuEngine *self);
FuSecurityAttrs *
fu_engine_get_host_security_events(FuEngine *self, guint limit, GError **error);
GHashTable *
fu_engine_get_report_metadata(FuEngine *self, GError **error);
gboolean
fu_engine_clear_results(FuEngine *self, const gchar *device_id, GError **error);
gboolean
fu_engine_update_metadata(FuEngine *self,
			  const gchar *remote_id,
			  gint fd,
			  gint fd_sig,
			  GError **error);
gboolean
fu_engine_update_metadata_bytes(FuEngine *self,
				const gchar *remote_id,
				GBytes *bytes_raw,
				GBytes *bytes_sig,
				GError **error);
gboolean
fu_engine_unlock(FuEngine *self, const gchar *device_id, GError **error);
gboolean
fu_engine_verify(FuEngine *self, const gchar *device_id, FuProgress *progress, GError **error);
gboolean
fu_engine_verify_update(FuEngine *self,
			const gchar *device_id,
			FuProgress *progress,
			GError **error);
GBytes *
fu_engine_firmware_dump(FuEngine *self,
			FuDevice *device,
			FuProgress *progress,
			FwupdInstallFlags flags,
			GError **error);
FuFirmware *
fu_engine_firmware_read(FuEngine *self,
			FuDevice *device,
			FuProgress *progress,
			FwupdInstallFlags flags,
			GError **error);
gboolean
fu_engine_modify_remote(FuEngine *self,
			const gchar *remote_id,
			const gchar *key,
			const gchar *value,
			GError **error);
gboolean
fu_engine_modify_device(FuEngine *self,
			const gchar *device_id,
			const gchar *key,
			const gchar *value,
			GError **error);
gboolean
fu_engine_composite_prepare(FuEngine *self, GPtrArray *devices, GError **error);
gboolean
fu_engine_composite_cleanup(FuEngine *self, GPtrArray *devices, GError **error);
gboolean
fu_engine_install_release(FuEngine *self,
			  FuRelease *release,
			  GBytes *blob_cab,
			  FuProgress *progress,
			  FwupdInstallFlags flags,
			  GError **error);
gboolean
fu_engine_install_blob(FuEngine *self,
		       FuDevice *device,
		       GBytes *blob_fw,
		       FuProgress *progress,
		       FwupdInstallFlags flags,
		       FwupdFeatureFlags feature_flags,
		       GError **error);
gboolean
fu_engine_install_releases(FuEngine *self,
			   FuEngineRequest *request,
			   GPtrArray *releases,
			   GBytes *blob_cab,
			   FuProgress *progress,
			   FwupdInstallFlags flags,
			   GError **error);
GPtrArray *
fu_engine_get_details(FuEngine *self, FuEngineRequest *request, gint fd, GError **error);
gboolean
fu_engine_activate(FuEngine *self, const gchar *device_id, FuProgress *progress, GError **error);
GPtrArray *
fu_engine_get_approved_firmware(FuEngine *self);
void
fu_engine_add_approved_firmware(FuEngine *self, const gchar *checksum);
void
fu_engine_set_approved_firmware(FuEngine *self, GPtrArray *checksums);
GPtrArray *
fu_engine_get_blocked_firmware(FuEngine *self);
gboolean
fu_engine_set_blocked_firmware(FuEngine *self, GPtrArray *checksums, GError **error);
gchar *
fu_engine_self_sign(FuEngine *self, const gchar *value, JcatSignFlags flags, GError **error);
gboolean
fu_engine_modify_config(FuEngine *self, const gchar *key, const gchar *value, GError **error);
gboolean
fu_engine_reset_config(FuEngine *self, const gchar *section, GError **error);
FuContext *
fu_engine_get_context(FuEngine *self);
GPtrArray *
fu_engine_get_releases_for_device(FuEngine *self,
				  FuEngineRequest *request,
				  FuDevice *device,
				  GError **error);

/* for the self tests */
void
fu_engine_add_device(FuEngine *self, FuDevice *device);
void
fu_engine_add_plugin(FuEngine *self, FuPlugin *plugin);
void
fu_engine_add_remote(FuEngine *self, FwupdRemote *remote);
void
fu_engine_add_runtime_version(FuEngine *self, const gchar *component_id, const gchar *version);
GPtrArray *
fu_engine_get_details_for_bytes(FuEngine *self,
				FuEngineRequest *request,
				GBytes *blob,
				GError **error);
gboolean
fu_engine_check_trust(FuEngine *self, FuRelease *release, GError **error);
void
fu_engine_set_silo(FuEngine *self, XbSilo *silo);
XbNode *
fu_engine_get_component_by_guids(FuEngine *self, FuDevice *device);
gchar *
fu_engine_get_remote_id_for_blob(FuEngine *self, GBytes *blob);
gboolean
fu_engine_schedule_update(FuEngine *self,
			  FuDevice *device,
			  FwupdRelease *release,
			  GBytes *blob_cab,
			  FwupdInstallFlags flags,
			  GError **error);
gboolean
fu_engine_modify_bios_settings(FuEngine *self,
			       GHashTable *settings,
			       gboolean force_ro,
			       GError **error);
gboolean
fu_engine_emulation_load(FuEngine *self, GBytes *data, GError **error);
GBytes *
fu_engine_emulation_save(FuEngine *self, GError **error);
gboolean
fu_engine_fix_host_security_attr(FuEngine *self, const gchar *appstream_id, GError **error);
gboolean
fu_engine_undo_host_security_attr(FuEngine *self, const gchar *appstream_id, GError **error);
