/*
 * Copyright 2026 Advanced Micro Devices, Inc.
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include "fu-amd-afc-bios-setting.h"
#include "fu-amd-afc-common.h"
#include "fu-amd-afc-setting.h"

struct _FuAmdAfcBiosSetting {
	FuBiosSetting parent_instance;
	FuAmdAfcState *state;
	guint index;
};

G_DEFINE_TYPE(FuAmdAfcBiosSetting, fu_amd_afc_bios_setting, FU_TYPE_BIOS_SETTING)

static gchar *
fu_amd_afc_bios_setting_read_value(FwupdBiosSetting *bios_setting, GError **error)
{
	FuAmdAfcBiosSetting *self = FU_AMD_AFC_BIOS_SETTING(bios_setting);
	FuAmdAfcSetting *setting;
	const gchar *name;

	setting = fu_amd_afc_state_get_setting(self->state, self->index, error);
	if (setting == NULL)
		return NULL;
	if (!setting->has_current) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_NOT_FOUND,
				    "AFC current value is unavailable");
		return NULL;
	}
	if (setting->kind == FU_AMD_AFC_SETTING_KIND_INTEGER)
		return g_strdup_printf("%" G_GUINT64_FORMAT, setting->current);
	name = fu_amd_afc_setting_get_option_name(setting, setting->current);
	if (name == NULL) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INVALID_DATA,
				    "AFC value has no matching option");
		return NULL;
	}
	return g_strdup(name);
}

static gboolean
fu_amd_afc_bios_setting_write_value(FwupdBiosSetting *bios_setting,
				    const gchar *value,
				    GError **error)
{
	FuAmdAfcBiosSetting *self = FU_AMD_AFC_BIOS_SETTING(bios_setting);
	return fu_amd_afc_state_store(self->state, self->index, value, error);
}

FuAmdAfcBiosSetting *
fu_amd_afc_bios_setting_new(FuAmdAfcState *state, guint index)
{
	FuContext *ctx = fu_amd_afc_state_get_context(state);
	FuAmdAfcBiosSetting *self =
	    g_object_new(FU_TYPE_AMD_AFC_BIOS_SETTING, "context", ctx, NULL);
	self->state = g_object_ref(state);
	self->index = index;
	return self;
}

static void
fu_amd_afc_bios_setting_finalize(GObject *object)
{
	FuAmdAfcBiosSetting *self = FU_AMD_AFC_BIOS_SETTING(object);
	g_clear_object(&self->state);
	G_OBJECT_CLASS(fu_amd_afc_bios_setting_parent_class)->finalize(object);
}

static void
fu_amd_afc_bios_setting_class_init(FuAmdAfcBiosSettingClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS(klass);
	FwupdBiosSettingClass *bios_setting_class = FWUPD_BIOS_SETTING_CLASS(klass);
	object_class->finalize = fu_amd_afc_bios_setting_finalize;
	bios_setting_class->read_value = fu_amd_afc_bios_setting_read_value;
	bios_setting_class->write_value = fu_amd_afc_bios_setting_write_value;
}

static void
fu_amd_afc_bios_setting_init(FuAmdAfcBiosSetting *self)
{
}
