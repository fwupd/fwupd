/*
 * Copyright (C) 2022 Mario Limonciello <mario.limonciello@amd.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#pragma once

#include <glib-object.h>

#include "fwupd-enums.h"

G_BEGIN_DECLS

#define FWUPD_TYPE_BIOS_SETTING (fwupd_bios_setting_get_type())
G_DECLARE_DERIVABLE_TYPE(FwupdBiosSetting, fwupd_bios_setting, FWUPD, BIOS_SETTING, GObject)

struct _FwupdBiosSettingClass {
	GObjectClass parent_class;
	/*< private >*/
	void (*_fwupd_reserved1)(void);
	void (*_fwupd_reserved2)(void);
	void (*_fwupd_reserved3)(void);
	void (*_fwupd_reserved4)(void);
	void (*_fwupd_reserved5)(void);
	void (*_fwupd_reserved6)(void);
	void (*_fwupd_reserved7)(void);
};

/* special attributes */
#define FWUPD_BIOS_SETTING_PENDING_REBOOT "pending_reboot"
#define FWUPD_BIOS_SETTING_RESET_BIOS	  "reset_bios"
#define FWUPD_BIOS_SETTING_DEBUG_CMD	  "debug_cmd"

/**
 * FwupdBiosSettingKind:
 * @FWUPD_BIOS_SETTING_KIND_UNKNOWN:		BIOS setting type is unknown
 * @FWUPD_BIOS_SETTING_KIND_ENUMERATION:		BIOS setting that has enumerated possible
 *values
 * @FWUPD_BIOS_SETTING_KIND_INTEGER:		BIOS setting that is an integer
 * @FWUPD_BIOS_SETTING_KIND_STRING:		BIOS setting that accepts a string
 *
 * The type of BIOS setting.
 **/
typedef enum {
	FWUPD_BIOS_SETTING_KIND_UNKNOWN = 0,	 /* Since: 1.8.4 */
	FWUPD_BIOS_SETTING_KIND_ENUMERATION = 1, /* Since: 1.8.4 */
	FWUPD_BIOS_SETTING_KIND_INTEGER = 2,	 /* Since: 1.8.4 */
	FWUPD_BIOS_SETTING_KIND_STRING = 3,	 /* Since: 1.8.4 */
	/*< private >*/
	FWUPD_BIOS_SETTING_KIND_LAST = 4 /* perhaps increased in the future */
} FwupdBiosSettingKind;

FwupdBiosSetting *
fwupd_bios_setting_new(const gchar *name, const gchar *path);
gchar *
fwupd_bios_setting_to_string(FwupdBiosSetting *self);

gboolean
fwupd_bios_setting_get_read_only(FwupdBiosSetting *self);
void
fwupd_bios_setting_set_read_only(FwupdBiosSetting *self, gboolean val);

guint64
fwupd_bios_setting_get_upper_bound(FwupdBiosSetting *self);
guint64
fwupd_bios_setting_get_lower_bound(FwupdBiosSetting *self);
guint64
fwupd_bios_setting_get_scalar_increment(FwupdBiosSetting *self);

void
fwupd_bios_setting_set_upper_bound(FwupdBiosSetting *self, guint64 val);
void
fwupd_bios_setting_set_lower_bound(FwupdBiosSetting *self, guint64 val);
void
fwupd_bios_setting_set_scalar_increment(FwupdBiosSetting *self, guint64 val);

void
fwupd_bios_setting_set_kind(FwupdBiosSetting *self, FwupdBiosSettingKind type);
void
fwupd_bios_setting_set_name(FwupdBiosSetting *self, const gchar *name);
void
fwupd_bios_setting_set_path(FwupdBiosSetting *self, const gchar *path);
void
fwupd_bios_setting_set_description(FwupdBiosSetting *self, const gchar *description);

FwupdBiosSettingKind
fwupd_bios_setting_get_kind(FwupdBiosSetting *self);
const gchar *
fwupd_bios_setting_get_name(FwupdBiosSetting *self);
const gchar *
fwupd_bios_setting_get_path(FwupdBiosSetting *self);
const gchar *
fwupd_bios_setting_get_description(FwupdBiosSetting *self);
const gchar *
fwupd_bios_setting_map_possible_value(FwupdBiosSetting *self, const gchar *key, GError **error);
gboolean
fwupd_bios_setting_has_possible_value(FwupdBiosSetting *self, const gchar *val);
void
fwupd_bios_setting_add_possible_value(FwupdBiosSetting *self, const gchar *possible_value);
GPtrArray *
fwupd_bios_setting_get_possible_values(FwupdBiosSetting *self);

FwupdBiosSetting *
fwupd_bios_setting_from_variant(GVariant *value);
GPtrArray *
fwupd_bios_setting_array_from_variant(GVariant *value);

const gchar *
fwupd_bios_setting_get_current_value(FwupdBiosSetting *self);
void
fwupd_bios_setting_set_current_value(FwupdBiosSetting *self, const gchar *value);

const gchar *
fwupd_bios_setting_get_id(FwupdBiosSetting *self);
void
fwupd_bios_setting_set_id(FwupdBiosSetting *self, const gchar *id);

G_END_DECLS
