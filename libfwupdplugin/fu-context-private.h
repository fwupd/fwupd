/*
 * Copyright (C) 2021 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#pragma once

#include "fu-context.h"
#include "fu-hwids.h"
#include "fu-quirks.h"
#include "fu-volume.h"

typedef enum {
	FU_CONTEXT_HWID_FLAG_NONE = 0,
	FU_CONTEXT_HWID_FLAG_LOAD_CONFIG = 1 << 0,
	FU_CONTEXT_HWID_FLAG_LOAD_SMBIOS = 1 << 1,
	FU_CONTEXT_HWID_FLAG_LOAD_FDT = 1 << 2,
	FU_CONTEXT_HWID_FLAG_LOAD_DMI = 1 << 3,
	FU_CONTEXT_HWID_FLAG_LOAD_KENV = 1 << 4,
	FU_CONTEXT_HWID_FLAG_LOAD_ALL = G_MAXUINT,
} FuContextHwidFlags;

FuContext *
fu_context_new(void);
gboolean
fu_context_reload_bios_settings(FuContext *self, GError **error);
gboolean
fu_context_load_hwinfo(FuContext *self, FuContextHwidFlags flags, GError **error);
gboolean
fu_context_load_quirks(FuContext *self, FuQuirksLoadFlags flags, GError **error);
void
fu_context_set_runtime_versions(FuContext *self, GHashTable *runtime_versions);
void
fu_context_set_compile_versions(FuContext *self, GHashTable *compile_versions);
void
fu_context_add_firmware_gtype(FuContext *self, const gchar *id, GType gtype);
GPtrArray *
fu_context_get_firmware_gtype_ids(FuContext *self);
GType
fu_context_get_firmware_gtype_by_id(FuContext *self, const gchar *id);
void
fu_context_add_udev_subsystem(FuContext *self, const gchar *subsystem);
GPtrArray *
fu_context_get_udev_subsystems(FuContext *self);
void
fu_context_add_esp_volume(FuContext *self, FuVolume *volume);
FuSmbios *
fu_context_get_smbios(FuContext *self);
FuHwids *
fu_context_get_hwids(FuContext *self);
void
fu_context_set_chassis_kind(FuContext *self, FuSmbiosChassisKind chassis_kind);
