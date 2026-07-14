/*
 * Copyright 2026 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include "fwupd-test-bios-setting.h"

struct _FwupdTestBiosSetting {
	FwupdBiosSetting parent_instance;
	gchar *value_raw;
};

G_DEFINE_TYPE(FwupdTestBiosSetting, fwupd_test_bios_setting, FWUPD_TYPE_BIOS_SETTING)

const gchar *
fwupd_test_bios_setting_get_value_raw(FwupdTestBiosSetting *self)
{
	return self->value_raw;
}

void
fwupd_test_bios_setting_set_value_raw(FwupdTestBiosSetting *self, const gchar *value_raw)
{
	g_free(self->value_raw);
	self->value_raw = g_strdup(value_raw);
}

static gboolean
fwupd_test_bios_setting_write_value(FwupdBiosSetting *setting, const gchar *value, GError **error)
{
	FwupdTestBiosSetting *self = FWUPD_TEST_BIOS_SETTING(setting);
	fwupd_test_bios_setting_set_value_raw(self, value);
	return TRUE;
}

static gchar *
fwupd_test_bios_setting_read_value(FwupdBiosSetting *setting, GError **error)
{
	FwupdTestBiosSetting *self = FWUPD_TEST_BIOS_SETTING(setting);
	return g_strdup(self->value_raw);
}

static void
fwupd_test_bios_setting_init(FwupdTestBiosSetting *self)
{
}

static void
fwupd_test_bios_setting_finalize(GObject *obj)
{
	FwupdTestBiosSetting *self = FWUPD_TEST_BIOS_SETTING(obj);
	g_free(self->value_raw);
	G_OBJECT_CLASS(fwupd_test_bios_setting_parent_class)->finalize(obj);
}

static void
fwupd_test_bios_setting_class_init(FwupdTestBiosSettingClass *klass)
{
	FwupdBiosSettingClass *bios_setting_class = FWUPD_BIOS_SETTING_CLASS(klass);
	GObjectClass *object_class = G_OBJECT_CLASS(klass);
	object_class->finalize = fwupd_test_bios_setting_finalize;
	bios_setting_class->write_value = fwupd_test_bios_setting_write_value;
	bios_setting_class->read_value = fwupd_test_bios_setting_read_value;
}

FwupdTestBiosSetting *
fwupd_test_bios_setting_new(void)
{
	return g_object_new(FWUPD_TYPE_TEST_BIOS_SETTING, NULL);
}
