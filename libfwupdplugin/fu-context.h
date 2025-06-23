/*
 * Copyright (C) 2021 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#pragma once

#include <gio/gio.h>

#include "fu-bios-settings.h"
#include "fu-common.h"
#include "fu-firmware.h"
#include "fu-smbios.h"

#define FU_TYPE_CONTEXT (fu_context_get_type())
G_DECLARE_DERIVABLE_TYPE(FuContext, fu_context, FU, CONTEXT, GObject)

struct _FuContextClass {
	GObjectClass parent_class;
	/* signals */
	void (*security_changed)(FuContext *self);
};

/**
 * FuContextLookupIter:
 * @self: a #FuContext
 * @key: a key
 * @value: a value
 * @user_data: user data
 *
 * The context lookup iteration callback.
 */
typedef void (*FuContextLookupIter)(FuContext *self,
				    const gchar *key,
				    const gchar *value,
				    gpointer user_data);

/**
 * FU_CONTEXT_FLAG_NONE:
 *
 * No flags set.
 *
 * Since: 1.8.5
 **/
#define FU_CONTEXT_FLAG_NONE (0u)

/**
 * FU_CONTEXT_FLAG_SAVE_EVENTS:
 *
 * Save events so that they can be replayed to emulate devices.
 *
 * Since: 1.8.5
 **/
#define FU_CONTEXT_FLAG_SAVE_EVENTS (1u << 0)

/**
 * FU_CONTEXT_FLAG_SYSTEM_INHIBIT:
 *
 * All devices are not updatable due to a system-wide inhibit.
 *
 * Since: 1.8.10
 **/
#define FU_CONTEXT_FLAG_SYSTEM_INHIBIT (1u << 1)

/**
 * FU_CONTEXT_FLAG_LOADED_HWINFO:
 *
 * Hardware information has been loaded with a call to fu_context_load_hwinfo().
 *
 * Since: 1.9.10
 **/
#define FU_CONTEXT_FLAG_LOADED_HWINFO (1u << 2)

/**
 * FU_CONTEXT_FLAG_IGNORE_EFIVARS_FREE_SPACE:
 *
 * Ignore the efivars free space requirement for db, dbx, KEK and PK updates.
 *
 * Since: 1.9.31, backported from 2.0.12
 **/
#define FU_CONTEXT_FLAG_IGNORE_EFIVARS_FREE_SPACE (1u << 6)

/**
 * FuContextFlags:
 *
 * The context flags.
 **/
typedef guint64 FuContextFlags;

void
fu_context_add_flag(FuContext *context, FuContextFlags flag) G_GNUC_NON_NULL(1);
void
fu_context_remove_flag(FuContext *context, FuContextFlags flag) G_GNUC_NON_NULL(1);
gboolean
fu_context_has_flag(FuContext *context, FuContextFlags flag) G_GNUC_NON_NULL(1);

const gchar *
fu_context_get_smbios_string(FuContext *self, guint8 structure_type, guint8 offset, GError **error)
    G_GNUC_NON_NULL(1);
guint
fu_context_get_smbios_integer(FuContext *self, guint8 type, guint8 offset, GError **error)
    G_GNUC_NON_NULL(1);
GPtrArray *
fu_context_get_smbios_data(FuContext *self, guint8 structure_type, GError **error)
    G_GNUC_NON_NULL(1);
gboolean
fu_context_has_hwid_guid(FuContext *self, const gchar *guid) G_GNUC_NON_NULL(1);
GPtrArray *
fu_context_get_hwid_guids(FuContext *self) G_GNUC_NON_NULL(1);
gboolean
fu_context_has_hwid_flag(FuContext *self, const gchar *flag) G_GNUC_NON_NULL(1, 2);
const gchar *
fu_context_get_hwid_value(FuContext *self, const gchar *key) G_GNUC_NON_NULL(1, 2);
gchar *
fu_context_get_hwid_replace_value(FuContext *self,
				  const gchar *keys,
				  GError **error) G_GNUC_WARN_UNUSED_RESULT G_GNUC_NON_NULL(1);
void
fu_context_add_runtime_version(FuContext *self, const gchar *component_id, const gchar *version)
    G_GNUC_NON_NULL(1, 2, 3);
const gchar *
fu_context_get_runtime_version(FuContext *self, const gchar *component_id) G_GNUC_NON_NULL(1, 2);
void
fu_context_add_compile_version(FuContext *self, const gchar *component_id, const gchar *version)
    G_GNUC_NON_NULL(1, 2, 3);
const gchar *
fu_context_lookup_quirk_by_id(FuContext *self, const gchar *guid, const gchar *key)
    G_GNUC_NON_NULL(1, 2, 3);
gboolean
fu_context_lookup_quirk_by_id_iter(FuContext *self,
				   const gchar *guid,
				   const gchar *key,
				   FuContextLookupIter iter_cb,
				   gpointer user_data) G_GNUC_NON_NULL(1, 2);
void
fu_context_add_quirk_key(FuContext *self, const gchar *key) G_GNUC_NON_NULL(1, 2);
void
fu_context_security_changed(FuContext *self) G_GNUC_NON_NULL(1);

FuPowerState
fu_context_get_power_state(FuContext *self) G_GNUC_NON_NULL(1);
void
fu_context_set_power_state(FuContext *self, FuPowerState power_state) G_GNUC_NON_NULL(1);
FuLidState
fu_context_get_lid_state(FuContext *self) G_GNUC_NON_NULL(1);
void
fu_context_set_lid_state(FuContext *self, FuLidState lid_state) G_GNUC_NON_NULL(1);
FuDisplayState
fu_context_get_display_state(FuContext *self) G_GNUC_NON_NULL(1);
void
fu_context_set_display_state(FuContext *self, FuDisplayState display_state) G_GNUC_NON_NULL(1);
guint
fu_context_get_battery_level(FuContext *self) G_GNUC_NON_NULL(1);
void
fu_context_set_battery_level(FuContext *self, guint battery_level) G_GNUC_NON_NULL(1);
guint
fu_context_get_battery_threshold(FuContext *self) G_GNUC_NON_NULL(1);
void
fu_context_set_battery_threshold(FuContext *self, guint battery_threshold) G_GNUC_NON_NULL(1);

FuBiosSettings *
fu_context_get_bios_settings(FuContext *self) G_GNUC_NON_NULL(1);
gboolean
fu_context_get_bios_setting_pending_reboot(FuContext *self) G_GNUC_NON_NULL(1);
FwupdBiosSetting *
fu_context_get_bios_setting(FuContext *self, const gchar *name) G_GNUC_NON_NULL(1, 2);

GPtrArray *
fu_context_get_esp_volumes(FuContext *self, GError **error) G_GNUC_WARN_UNUSED_RESULT
    G_GNUC_NON_NULL(1);
FuFirmware *
fu_context_get_fdt(FuContext *self, GError **error) G_GNUC_WARN_UNUSED_RESULT G_GNUC_NON_NULL(1);
FuSmbiosChassisKind
fu_context_get_chassis_kind(FuContext *self) G_GNUC_NON_NULL(1);
void
fu_context_set_esp_location(FuContext *self, const gchar *location);
const gchar *
fu_context_get_esp_location(FuContext *self);

gboolean
fu_context_efivars_check_free_space(FuContext *self, gsize count, GError **error)
    G_GNUC_NON_NULL(1);
