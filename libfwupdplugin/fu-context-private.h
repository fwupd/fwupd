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
fu_context_add_firmware_gtype(FuContext *self, GType gtype) G_GNUC_NON_NULL(1);
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
void
fu_context_set_smbios(FuContext *self, FuSmbios *smbios) G_GNUC_NON_NULL(1, 2);
FuHwids *
fu_context_get_hwids(FuContext *self) G_GNUC_NON_NULL(1);
void
fu_context_set_hwids(FuContext *self, FuHwids *hwids) G_GNUC_NON_NULL(1, 2);
FuConfig *
fu_context_get_config(FuContext *self) G_GNUC_NON_NULL(1);
void
fu_context_set_chassis_kind(FuContext *self, FuSmbiosChassisKind chassis_kind) G_GNUC_NON_NULL(1);

gpointer
fu_context_get_data(FuContext *self, const gchar *key);
void
fu_context_set_data(FuContext *self, const gchar *key, gpointer data);
void
fu_context_reset_config(FuContext *self) G_GNUC_NON_NULL(1);
