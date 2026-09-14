/*
 * Copyright 2022 Mario Limonciello <mario.limonciello@amd.com>
 * Copyright 2026 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include "fu-linux-fwattr-setting.h"

struct _FuLinuxFwattrSetting {
	FuBiosSetting parent_instance;
};

G_DEFINE_TYPE(FuLinuxFwattrSetting, fu_linux_fwattr_setting, FU_TYPE_BIOS_SETTING)

#define LENOVO_READ_ONLY_NEEDLE "[Status:ShowOnly]"

static gboolean
fu_linux_fwattr_setting_read_file(FuLinuxFwattrSetting *self, /* nocheck:name */
				  const gchar *key,
				  gchar **value_out,
				  GError **error)
{
	g_autofree gchar *tmp = NULL;

	g_return_val_if_fail(value_out != NULL, FALSE);

	tmp = g_build_filename(fu_bios_setting_get_path(FU_BIOS_SETTING(self)), key, NULL);
	if (!g_file_get_contents(tmp, value_out, NULL, error)) {
		g_prefix_error(error, "failed to load %s: ", key);
		fwupd_error_convert(error);
		return FALSE;
	}
	g_strchomp(*value_out);
	return TRUE;
}

static gchar *
fu_linux_fwattr_setting_read_value(FwupdBiosSetting *attr, GError **error)
{
	FuLinuxFwattrSetting *self = FU_LINUX_FWATTR_SETTING(attr);
	struct {
		const gchar *id;
		const gchar *current_value;
	} map[] = {
	    {"com.thinklmi.SecureBoot", "Enable"},
	    {"com.dell-wmi-sysman.SecureBoot", "Enabled"},
	};
	const gchar *tmp;
	g_autofree gchar *data = NULL;

	if (!fu_linux_fwattr_setting_read_file(self,
					       fwupd_bios_setting_get_filename(attr),
					       &data,
					       error))
		return NULL;
	if (fu_bios_setting_get_kind(attr) == FWUPD_BIOS_SETTING_KIND_ENUMERATION) {
		for (guint i = 0; i < G_N_ELEMENTS(map); i++) {
			if (g_strcmp0(fu_bios_setting_get_id(attr), map[i].id) == 0 &&
			    g_strcmp0(data, map[i].current_value) == 0) {
				fu_bios_setting_set_read_only(attr, TRUE);
			}
		}
	}
	tmp = g_strrstr(data, LENOVO_READ_ONLY_NEEDLE);
	if (tmp != NULL)
		fu_bios_setting_set_read_only(attr, TRUE);

	return g_steal_pointer(&data);
}

static gboolean
fu_linux_fwattr_setting_ensure_description(FuLinuxFwattrSetting *self, GError **error)
{
	g_autofree gchar *data = NULL;

	/* already set from quirk */
	if (fu_bios_setting_get_description(FU_BIOS_SETTING(self)) != NULL)
		return TRUE;

	if (!fu_linux_fwattr_setting_read_file(self, "display_name", &data, error))
		return FALSE;
	fu_bios_setting_set_description(FU_BIOS_SETTING(self), data);
	return TRUE;
}

static guint64
fu_linux_fwattr_setting_read_file_as_integer(FuLinuxFwattrSetting *self, /* nocheck:name */
					     const gchar *key,
					     GError **error)
{
	g_autofree gchar *str = NULL;
	guint64 tmp;

	if (!fu_linux_fwattr_setting_read_file(self, key, &str, error))
		return G_MAXUINT64;
	if (!fu_strtoull(str, &tmp, 0, G_MAXUINT64 - 1, FU_INTEGER_BASE_AUTO, error)) {
		g_prefix_error(error, "failed to convert %s to integer: ", key);
		return G_MAXUINT64;
	}
	return tmp;
}

static gboolean
fu_bios_setting_ensure_enumeration_attrs(FuLinuxFwattrSetting *self,
					 GError **error) /* nocheck:name */
{
	const gchar *delimiters[] = {",", ";", NULL};
	g_autofree gchar *str = NULL;

	if (!fu_linux_fwattr_setting_read_file(self, "possible_values", &str, error))
		return FALSE;
	for (guint j = 0; delimiters[j] != NULL; j++) {
		g_auto(GStrv) vals = NULL;
		if (g_strrstr(str, delimiters[j]) == NULL)
			continue;
		vals = fu_strsplit(str, strlen(str), delimiters[j], -1);
		if (vals[0] != NULL)
			fu_bios_setting_set_kind(FU_BIOS_SETTING(self),
						 FWUPD_BIOS_SETTING_KIND_ENUMERATION);
		for (guint i = 0; vals[i] != NULL && vals[i][0] != '\0'; i++)
			fu_bios_setting_add_possible_value(FU_BIOS_SETTING(self), vals[i]);
	}
	return TRUE;
}

static gboolean
fu_linux_fwattr_setting_ensure_string_attrs(FuLinuxFwattrSetting *self,
					    GError **error) /* nocheck:name */
{
	guint64 tmp;

	tmp = fu_linux_fwattr_setting_read_file_as_integer(self, "min_length", error);
	if (tmp == G_MAXUINT64)
		return FALSE;
	fu_bios_setting_set_lower_bound(FU_BIOS_SETTING(self), tmp);
	tmp = fu_linux_fwattr_setting_read_file_as_integer(self, "max_length", error);
	if (tmp == G_MAXUINT64)
		return FALSE;
	fu_bios_setting_set_upper_bound(FU_BIOS_SETTING(self), tmp);
	fu_bios_setting_set_kind(FU_BIOS_SETTING(self), FWUPD_BIOS_SETTING_KIND_STRING);
	return TRUE;
}

static gboolean
fu_linux_fwattr_setting_ensure_integer_attrs(FuLinuxFwattrSetting *self,
					     GError **error) /* nocheck:name */
{
	guint64 tmp;

	tmp = fu_linux_fwattr_setting_read_file_as_integer(self, "min_value", error);
	if (tmp == G_MAXUINT64)
		return FALSE;
	fu_bios_setting_set_lower_bound(FU_BIOS_SETTING(self), tmp);
	tmp = fu_linux_fwattr_setting_read_file_as_integer(self, "max_value", error);
	if (tmp == G_MAXUINT64)
		return FALSE;
	fu_bios_setting_set_upper_bound(FU_BIOS_SETTING(self), tmp);
	tmp = fu_linux_fwattr_setting_read_file_as_integer(self, "scalar_increment", error);
	if (tmp == G_MAXUINT64)
		return FALSE;
	fu_bios_setting_set_scalar_increment(FU_BIOS_SETTING(self), tmp);
	fu_bios_setting_set_kind(FU_BIOS_SETTING(self), FWUPD_BIOS_SETTING_KIND_INTEGER);
	return TRUE;
}

static gboolean
fu_linux_fwattr_setting_ensure_type(FuLinuxFwattrSetting *self, GError **error)
{
	g_autofree gchar *data = NULL;
	g_autoptr(GError) error_local = NULL;

	if (!fu_linux_fwattr_setting_read_file(self, "type", &data, &error_local)) {
		g_debug("%s", error_local->message);
		g_propagate_error(error, g_steal_pointer(&error_local));
		return FALSE;
	}

	if (g_strcmp0(data, "enumeration") == 0) {
		if (!fu_bios_setting_ensure_enumeration_attrs(self, &error_local))
			g_debug("failed to add enumeration attrs: %s", error_local->message);
	} else if (g_strcmp0(data, "integer") == 0) {
		if (!fu_linux_fwattr_setting_ensure_integer_attrs(self, &error_local))
			g_debug("failed to add integer attrs: %s", error_local->message);
	} else if (g_strcmp0(data, "string") == 0) {
		if (!fu_linux_fwattr_setting_ensure_string_attrs(self, &error_local))
			g_debug("failed to add string attrs: %s", error_local->message);
	}

	/* success */
	return TRUE;
}

/* Special case attribute that is a file not a folder
 * https://github.com/torvalds/linux/blob/v5.18/Documentation/ABI/testing/sysfs-class-firmware-attributes#L300
 */
static gboolean
fu_linux_fwattr_setting_ensure_file_attributes(FuLinuxFwattrSetting *self, GError **error)
{
	g_autofree gchar *value = NULL;

	if (g_strcmp0(fu_bios_setting_get_name(FU_BIOS_SETTING(self)),
		      FWUPD_BIOS_SETTING_PENDING_REBOOT) != 0) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_NOT_SUPPORTED,
			    "%s attribute is not supported",
			    fu_bios_setting_get_name(FU_BIOS_SETTING(self)));
		return FALSE;
	}
	if (!fu_linux_fwattr_setting_read_file(self, NULL, &value, error))
		return FALSE;
	fwupd_bios_setting_set_current_value(FWUPD_BIOS_SETTING(FU_BIOS_SETTING(self)), value);
	fu_bios_setting_set_read_only(FU_BIOS_SETTING(self), TRUE);

	return TRUE;
}

static gboolean
fu_linux_fwattr_setting_ensure_folder_attributes(FuLinuxFwattrSetting *self, GError **error)
{
	g_autoptr(GError) error_local = NULL;

	if (!fu_linux_fwattr_setting_ensure_type(self, error))
		return FALSE;
	if (!fwupd_bios_setting_setup(FWUPD_BIOS_SETTING(self), error))
		return FALSE;
	if (!fu_linux_fwattr_setting_ensure_description(self, &error_local))
		g_debug("%s", error_local->message);
	return TRUE;
}

FuLinuxFwattrSetting *
fu_linux_fwattr_setting_new(FuContext *ctx,
			    const gchar *driver,
			    const gchar *path,
			    const gchar *name,
			    GError **error)
{
	g_autofree gchar *id = NULL;
	g_autofree gchar *name_stripped = NULL;
	g_autoptr(FuLinuxFwattrSetting) self =
	    g_object_new(FU_TYPE_LINUX_FWATTR_SETTING, "context", ctx, NULL);

	g_return_val_if_fail(name != NULL, NULL);
	g_return_val_if_fail(path != NULL, NULL);
	g_return_val_if_fail(driver != NULL, NULL);

	name_stripped = g_strdup(name);
	g_strdelimit(name_stripped, " ", '_');

	fu_bios_setting_set_name(FU_BIOS_SETTING(self), name);
	fu_bios_setting_set_path(FU_BIOS_SETTING(self), path);
	id = g_strdup_printf("com.%s.%s", driver, name_stripped);
	fu_bios_setting_set_id(FU_BIOS_SETTING(self), id);

	if (g_file_test(path, G_FILE_TEST_IS_DIR)) {
		fu_bios_setting_set_filename(FU_BIOS_SETTING(self), "current_value");
		if (!fu_linux_fwattr_setting_ensure_folder_attributes(self, error))
			return NULL;
	} else {
		if (!fu_linux_fwattr_setting_ensure_file_attributes(self, error))
			return NULL;
	}

	/* success */
	return g_steal_pointer(&self);
}

static gboolean
fu_linux_fwattr_setting_write_value(FwupdBiosSetting *self, const gchar *value, GError **error)
{
	g_autofree gchar *fn = NULL;
	g_autoptr(FuIOChannel) io = NULL;

	if (fwupd_bios_setting_get_path(self) == NULL)
		return TRUE;

	fn = g_build_filename(fwupd_bios_setting_get_path(self),
			      fwupd_bios_setting_get_filename(self),
			      NULL);
	io = fu_io_channel_new_file(fn, FU_IO_CHANNEL_OPEN_FLAG_WRITE, error);
	if (io == NULL)
		return FALSE;
	if (!fu_io_channel_write_raw(io,
				     (const guint8 *)value,
				     strlen(value),
				     1000,
				     FU_IO_CHANNEL_FLAG_NONE,
				     error))
		return FALSE;

	/* success */
	fwupd_bios_setting_set_current_value(self, value);
	g_debug("set %s to %s", fwupd_bios_setting_get_id(self), value);
	return TRUE;
}

static void
fu_linux_fwattr_setting_init(FuLinuxFwattrSetting *self)
{
}

static void
fu_linux_fwattr_setting_class_init(FuLinuxFwattrSettingClass *klass)
{
	FwupdBiosSettingClass *bios_setting_class = FWUPD_BIOS_SETTING_CLASS(klass);
	bios_setting_class->write_value = fu_linux_fwattr_setting_write_value;
	bios_setting_class->read_value = fu_linux_fwattr_setting_read_value;
}
