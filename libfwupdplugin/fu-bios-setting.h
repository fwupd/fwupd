/*
 * Copyright 2023 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include <fwupd.h>

G_BEGIN_DECLS

#define FU_TYPE_BIOS_SETTING (fu_bios_setting_get_type())

G_DECLARE_DERIVABLE_TYPE(FuBiosSetting, fu_bios_setting, FU, BIOS_SETTING, FwupdBiosSetting)

struct _FuBiosSettingClass {
	FwupdBiosSettingClass parent_class;
};

FuBiosSetting *
fu_bios_setting_new(gpointer ctx);

void
fu_bios_setting_set_id(FuBiosSetting *self, const gchar *id);
void
fu_bios_setting_set_appstream_id(FuBiosSetting *self, const gchar *appstream_id);
void
fu_bios_setting_set_name(FuBiosSetting *self, const gchar *name);

#define fu_bios_setting_add_possible_value(r, v)                                                   \
	fwupd_bios_setting_add_possible_value(FWUPD_BIOS_SETTING(r), v)
#define fu_bios_setting_get_read_only(r) fwupd_bios_setting_get_read_only(FWUPD_BIOS_SETTING(r))
#define fu_bios_setting_get_possible_values(r)                                                     \
	fwupd_bios_setting_get_possible_values(FWUPD_BIOS_SETTING(r))
#define fu_bios_setting_get_description(r) fwupd_bios_setting_get_description(FWUPD_BIOS_SETTING(r))
#define fu_bios_setting_get_lower_bound(r) fwupd_bios_setting_get_lower_bound(FWUPD_BIOS_SETTING(r))
#define fu_bios_setting_get_upper_bound(r) fwupd_bios_setting_get_upper_bound(FWUPD_BIOS_SETTING(r))
#define fu_bios_setting_get_scalar_increment(r)                                                    \
	fwupd_bios_setting_get_scalar_increment(FWUPD_BIOS_SETTING(r))
#define fu_bios_setting_get_icon(r) fwupd_bios_setting_get_icon(FWUPD_BIOS_SETTING(r))
#define fu_bios_setting_write_value(r, v, e)                                                       \
	fwupd_bios_setting_write_value(FWUPD_BIOS_SETTING(r), v, e)
#define fu_bios_setting_get_filename(r) fwupd_bios_setting_get_filename(FWUPD_BIOS_SETTING(r))
#define fu_bios_setting_get_appstream_id(r)                                                        \
	fwupd_bios_setting_get_appstream_id(FWUPD_BIOS_SETTING(r))
#define fu_bios_setting_get_current_value(r)                                                       \
	fwupd_bios_setting_get_current_value(FWUPD_BIOS_SETTING(r))
#define fu_bios_setting_get_id(r)   fwupd_bios_setting_get_id(FWUPD_BIOS_SETTING(r))
#define fu_bios_setting_get_kind(r) fwupd_bios_setting_get_kind(FWUPD_BIOS_SETTING(r))
#define fu_bios_setting_get_name(r) fwupd_bios_setting_get_name(FWUPD_BIOS_SETTING(r))
#define fu_bios_setting_get_path(r) fwupd_bios_setting_get_path(FWUPD_BIOS_SETTING(r))
#define fu_bios_setting_set_current_value(r, v)                                                    \
	fwupd_bios_setting_set_current_value(FWUPD_BIOS_SETTING(r), v)
#define fu_bios_setting_set_description(r, v)                                                      \
	fwupd_bios_setting_set_description(FWUPD_BIOS_SETTING(r), v)
#define fu_bios_setting_set_filename(r, v) fwupd_bios_setting_set_filename(FWUPD_BIOS_SETTING(r), v)
#define fu_bios_setting_set_icon(r, v)	   fwupd_bios_setting_set_icon(FWUPD_BIOS_SETTING(r), v)
#define fu_bios_setting_set_kind(r, v)	   fwupd_bios_setting_set_kind(FWUPD_BIOS_SETTING(r), v)
#define fu_bios_setting_set_lower_bound(r, v)                                                      \
	fwupd_bios_setting_set_lower_bound(FWUPD_BIOS_SETTING(r), v)
#define fu_bios_setting_set_path(r, v)	   fwupd_bios_setting_set_path(FWUPD_BIOS_SETTING(r), v)
#define fu_bios_setting_set_read_only(r, v)                                                        \
	fwupd_bios_setting_set_read_only(FWUPD_BIOS_SETTING(r), v)
#define fu_bios_setting_set_scalar_increment(r, v)                                                 \
	fwupd_bios_setting_set_scalar_increment(FWUPD_BIOS_SETTING(r), v)
#define fu_bios_setting_set_type(r, v)	   fwupd_bios_setting_set_type(FWUPD_BIOS_SETTING(r), v)
#define fu_bios_setting_set_upper_bound(r, v)                                                      \
	fwupd_bios_setting_set_upper_bound(FWUPD_BIOS_SETTING(r), v)

G_END_DECLS
