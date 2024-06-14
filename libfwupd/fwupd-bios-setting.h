/*
 * Copyright 2022 Mario Limonciello <mario.limonciello@amd.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include <glib-object.h>

#include "fwupd-build.h"
#include "fwupd-enums.h"

G_BEGIN_DECLS

#define FWUPD_TYPE_BIOS_SETTING (fwupd_bios_setting_get_type())
G_DECLARE_DERIVABLE_TYPE(FwupdBiosSetting, fwupd_bios_setting, FWUPD, BIOS_SETTING, GObject)

struct _FwupdBiosSettingClass {
	GObjectClass parent_class;
	gboolean (*write_value)(FwupdBiosSetting *self, const gchar *value, GError **error);
	/*< private >*/
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
 *
 * The type of BIOS setting.
 **/
typedef enum {
	/**
	 * FWUPD_BIOS_SETTING_KIND_UNKNOWN:
	 *
	 * BIOS setting type is unknown.
	 *
	 * Since: 1.8.4
	 */
	FWUPD_BIOS_SETTING_KIND_UNKNOWN = 0,
	/**
	 * FWUPD_BIOS_SETTING_KIND_ENUMERATION:
	 *
	 * BIOS setting that has enumerated possible values.
	 *
	 * Since: 1.8.4
	 */
	FWUPD_BIOS_SETTING_KIND_ENUMERATION = 1,
	/**
	 * FWUPD_BIOS_SETTING_KIND_INTEGER:
	 *
	 * BIOS setting that is an integer.
	 *
	 * Since: 1.8.4
	 */
	FWUPD_BIOS_SETTING_KIND_INTEGER = 2,
	/**
	 * FWUPD_BIOS_SETTING_KIND_STRING:
	 *
	 * BIOS setting that accepts a string.
	 *
	 * Since: 1.8.4
	 */
	FWUPD_BIOS_SETTING_KIND_STRING = 3,
	/*< private >*/
	FWUPD_BIOS_SETTING_KIND_LAST = 4
} FwupdBiosSettingKind;

FwupdBiosSetting *
fwupd_bios_setting_new(const gchar *name, const gchar *path);

gboolean
fwupd_bios_setting_get_read_only(FwupdBiosSetting *self) G_GNUC_NON_NULL(1);
void
fwupd_bios_setting_set_read_only(FwupdBiosSetting *self, gboolean val) G_GNUC_NON_NULL(1);

guint64
fwupd_bios_setting_get_upper_bound(FwupdBiosSetting *self) G_GNUC_NON_NULL(1);
guint64
fwupd_bios_setting_get_lower_bound(FwupdBiosSetting *self) G_GNUC_NON_NULL(1);
guint64
fwupd_bios_setting_get_scalar_increment(FwupdBiosSetting *self) G_GNUC_NON_NULL(1);

void
fwupd_bios_setting_set_upper_bound(FwupdBiosSetting *self, guint64 val) G_GNUC_NON_NULL(1);
void
fwupd_bios_setting_set_lower_bound(FwupdBiosSetting *self, guint64 val) G_GNUC_NON_NULL(1);
void
fwupd_bios_setting_set_scalar_increment(FwupdBiosSetting *self, guint64 val) G_GNUC_NON_NULL(1);

void
fwupd_bios_setting_set_kind(FwupdBiosSetting *self, FwupdBiosSettingKind type) G_GNUC_NON_NULL(1);
void
fwupd_bios_setting_set_name(FwupdBiosSetting *self, const gchar *name) G_GNUC_NON_NULL(1);
void
fwupd_bios_setting_set_path(FwupdBiosSetting *self, const gchar *path) G_GNUC_NON_NULL(1);
void
fwupd_bios_setting_set_description(FwupdBiosSetting *self, const gchar *description)
    G_GNUC_NON_NULL(1);

FwupdBiosSettingKind
fwupd_bios_setting_get_kind(FwupdBiosSetting *self) G_GNUC_NON_NULL(1);
const gchar *
fwupd_bios_setting_get_name(FwupdBiosSetting *self) G_GNUC_NON_NULL(1);
const gchar *
fwupd_bios_setting_get_path(FwupdBiosSetting *self) G_GNUC_NON_NULL(1);
const gchar *
fwupd_bios_setting_get_description(FwupdBiosSetting *self) G_GNUC_NON_NULL(1);
const gchar *
fwupd_bios_setting_map_possible_value(FwupdBiosSetting *self, const gchar *key, GError **error)
    G_GNUC_NON_NULL(1, 2);
gboolean
fwupd_bios_setting_has_possible_value(FwupdBiosSetting *self,
				      const gchar *val) G_GNUC_WARN_UNUSED_RESULT
    G_GNUC_NON_NULL(1, 2);
void
fwupd_bios_setting_add_possible_value(FwupdBiosSetting *self, const gchar *possible_value)
    G_GNUC_NON_NULL(1, 2);
GPtrArray *
fwupd_bios_setting_get_possible_values(FwupdBiosSetting *self) G_GNUC_NON_NULL(1);

const gchar *
fwupd_bios_setting_get_current_value(FwupdBiosSetting *self) G_GNUC_NON_NULL(1);
void
fwupd_bios_setting_set_current_value(FwupdBiosSetting *self, const gchar *value) G_GNUC_NON_NULL(1);

gboolean
fwupd_bios_setting_write_value(FwupdBiosSetting *self, const gchar *value, GError **error)
    G_GNUC_NON_NULL(1, 2);

const gchar *
fwupd_bios_setting_get_id(FwupdBiosSetting *self) G_GNUC_NON_NULL(1);
void
fwupd_bios_setting_set_id(FwupdBiosSetting *self, const gchar *id) G_GNUC_NON_NULL(1);

G_END_DECLS
