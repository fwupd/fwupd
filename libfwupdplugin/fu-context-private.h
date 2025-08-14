/*
 * Copyright 2021 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include "fu-config.h"
#include "fu-context.h"
#include "fu-hwids.h"
#include "fu-progress.h"
#include "fu-quirks.h"
#include "fu-volume.h"

typedef enum {
	FU_CONTEXT_HWID_FLAG_NONE = 0,
	FU_CONTEXT_HWID_FLAG_LOAD_CONFIG = 1 << 0,
	FU_CONTEXT_HWID_FLAG_LOAD_SMBIOS = 1 << 1,
	FU_CONTEXT_HWID_FLAG_LOAD_FDT = 1 << 2,
	FU_CONTEXT_HWID_FLAG_LOAD_DMI = 1 << 3,
	FU_CONTEXT_HWID_FLAG_LOAD_KENV = 1 << 4,
	FU_CONTEXT_HWID_FLAG_LOAD_DARWIN = 1 << 5,
	FU_CONTEXT_HWID_FLAG_WATCH_FILES = 1 << 6,
	FU_CONTEXT_HWID_FLAG_FIX_PERMISSIONS = 1 << 7,
} FuContextHwidFlags;

#define FU_CONTEXT_HWID_FLAG_LOAD_ALL                                                              \
	(FU_CONTEXT_HWID_FLAG_LOAD_CONFIG | FU_CONTEXT_HWID_FLAG_LOAD_SMBIOS |                     \
	 FU_CONTEXT_HWID_FLAG_LOAD_FDT | FU_CONTEXT_HWID_FLAG_LOAD_DMI |                           \
	 FU_CONTEXT_HWID_FLAG_LOAD_KENV | FU_CONTEXT_HWID_FLAG_LOAD_DARWIN)

FuContext *
fu_context_new(void);
void
fu_context_housekeeping(FuContext *self) G_GNUC_NON_NULL(1);
gboolean
fu_context_reload_bios_settings(FuContext *self, GError **error);
gboolean
fu_context_load_hwinfo(FuContext *self,
		       FuProgress *progress,
		       FuContextHwidFlags flags,
		       GError **error) G_GNUC_NON_NULL(1);
gboolean
fu_context_load_quirks(FuContext *self, FuQuirksLoadFlags flags, GError **error) G_GNUC_NON_NULL(1);
GHashTable *
fu_context_get_runtime_versions(FuContext *self) G_GNUC_NON_NULL(1);
GHashTable *
fu_context_get_compile_versions(FuContext *self) G_GNUC_NON_NULL(1);
void
fu_context_add_firmware_gtype(FuContext *self, const gchar *id, GType gtype) G_GNUC_NON_NULL(1, 2);
GPtrArray *
fu_context_get_firmware_gtype_ids(FuContext *self) G_GNUC_NON_NULL(1);
GArray *
fu_context_get_firmware_gtypes(FuContext *self) G_GNUC_NON_NULL(1);
GType
fu_context_get_firmware_gtype_by_id(FuContext *self, const gchar *id) G_GNUC_NON_NULL(1, 2);
void
fu_context_add_udev_subsystem(FuContext *self, const gchar *subsystem, const gchar *plugin_name)
    G_GNUC_NON_NULL(1, 2);
GPtrArray *
fu_context_get_udev_subsystems(FuContext *self) G_GNUC_NON_NULL(1);
GPtrArray *
fu_context_get_backends(FuContext *self) G_GNUC_NON_NULL(1);
gboolean
fu_context_has_backend(FuContext *self, const gchar *name) G_GNUC_NON_NULL(1, 2);
GPtrArray *
fu_context_get_plugin_names_for_udev_subsystem(FuContext *self,
					       const gchar *subsystem,
					       GError **error) G_GNUC_NON_NULL(1, 2);
void
fu_context_add_esp_volume(FuContext *self, FuVolume *volume) G_GNUC_NON_NULL(1);
FuSmbios *
fu_context_get_smbios(FuContext *self) G_GNUC_NON_NULL(1);
FuHwids *
fu_context_get_hwids(FuContext *self) G_GNUC_NON_NULL(1);
FuConfig *
fu_context_get_config(FuContext *self) G_GNUC_NON_NULL(1);
void
fu_context_set_chassis_kind(FuContext *self, FuSmbiosChassisKind chassis_kind) G_GNUC_NON_NULL(1);

gpointer
fu_context_get_data(FuContext *self, const gchar *key);
void
fu_context_set_data(FuContext *self, const gchar *key, gpointer data);
