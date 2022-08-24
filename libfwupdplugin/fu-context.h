/*
 * Copyright (C) 2021 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#pragma once

#include <gio/gio.h>

#include "fu-bios-settings.h"
#include "fu-common.h"

#define FU_TYPE_CONTEXT (fu_context_get_type())
G_DECLARE_DERIVABLE_TYPE(FuContext, fu_context, FU, CONTEXT, GObject)

struct _FuContextClass {
	GObjectClass parent_class;
	/* signals */
	void (*security_changed)(FuContext *self);
	/*< private >*/
	gpointer padding[30];
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

const gchar *
fu_context_get_smbios_string(FuContext *self, guint8 structure_type, guint8 offset);
guint
fu_context_get_smbios_integer(FuContext *self, guint8 type, guint8 offset);
GBytes *
fu_context_get_smbios_data(FuContext *self, guint8 structure_type);
gboolean
fu_context_has_hwid_guid(FuContext *self, const gchar *guid);
GPtrArray *
fu_context_get_hwid_guids(FuContext *self);
gboolean
fu_context_has_hwid_flag(FuContext *self, const gchar *flag);
const gchar *
fu_context_get_hwid_value(FuContext *self, const gchar *key);
gchar *
fu_context_get_hwid_replace_value(FuContext *self,
				  const gchar *keys,
				  GError **error) G_GNUC_WARN_UNUSED_RESULT;
void
fu_context_add_runtime_version(FuContext *self, const gchar *component_id, const gchar *version);
void
fu_context_add_compile_version(FuContext *self, const gchar *component_id, const gchar *version);
const gchar *
fu_context_lookup_quirk_by_id(FuContext *self, const gchar *guid, const gchar *key);
gboolean
fu_context_lookup_quirk_by_id_iter(FuContext *self,
				   const gchar *guid,
				   FuContextLookupIter iter_cb,
				   gpointer user_data);
void
fu_context_add_quirk_key(FuContext *self, const gchar *key);
void
fu_context_security_changed(FuContext *self);

FuBatteryState
fu_context_get_battery_state(FuContext *self);
void
fu_context_set_battery_state(FuContext *self, FuBatteryState battery_state);
FuLidState
fu_context_get_lid_state(FuContext *self);
void
fu_context_set_lid_state(FuContext *self, FuLidState lid_state);
guint
fu_context_get_battery_level(FuContext *self);
void
fu_context_set_battery_level(FuContext *self, guint battery_level);
guint
fu_context_get_battery_threshold(FuContext *self);
void
fu_context_set_battery_threshold(FuContext *self, guint battery_threshold);

FuBiosSettings *
fu_context_get_bios_settings(FuContext *self);
gboolean
fu_context_get_bios_setting_pending_reboot(FuContext *self);
FwupdBiosSetting *
fu_context_get_bios_setting(FuContext *self, const gchar *name);
