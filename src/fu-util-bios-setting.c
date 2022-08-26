/*
 * Copyright (C) 2022 Mario Limonciello <mario.limonciello@amd.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#define G_LOG_DOMAIN "FuMain"

#include "config.h"

#include <glib/gi18n.h>

#include "fu-bios-settings-private.h"
#include "fu-util-bios-setting.h"
#include "fu-util-common.h"

static void
fu_util_bios_setting_update_description(FwupdBiosSetting *setting)
{
	const gchar *new = NULL;

	/* try to look it up from translations */
	new = gettext(fwupd_bios_setting_get_description(setting));
	if (new != NULL)
		fwupd_bios_setting_set_description(setting, new);
}

static const gchar *
fu_util_bios_setting_kind_to_string(FwupdBiosSettingKind kind)
{
	if (kind == FWUPD_BIOS_SETTING_KIND_ENUMERATION) {
		/* TRANSLATORS: The BIOS setting can only be changed to fixed values */
		return _("Enumeration");
	}
	if (kind == FWUPD_BIOS_SETTING_KIND_INTEGER) {
		/* TRANSLATORS: The BIOS setting only accepts integers in a fixed range */
		return _("Integer");
	}
	if (kind == FWUPD_BIOS_SETTING_KIND_STRING) {
		/* TRANSLATORS: The BIOS setting accepts strings */
		return _("String");
	}
	return NULL;
}

gboolean
fu_util_bios_setting_matches_args(FwupdBiosSetting *setting, gchar **values)
{
	const gchar *name;

	/* no arguments set */
	if (g_strv_length(values) == 0)
		return TRUE;
	name = fwupd_bios_setting_get_name(setting);

	/* check all arguments */
	for (guint j = 0; j < g_strv_length(values); j++) {
		if (g_strcmp0(name, values[j]) == 0)
			return TRUE;
	}
	return FALSE;
}

gboolean
fu_util_get_bios_setting_as_json(gchar **values, GPtrArray *settings, GError **error)
{
	g_autoptr(JsonBuilder) builder = json_builder_new();
	json_builder_begin_object(builder);

	json_builder_set_member_name(builder, "BiosSettings");
	json_builder_begin_array(builder);
	for (guint i = 0; i < settings->len; i++) {
		FwupdBiosSetting *setting = g_ptr_array_index(settings, i);
		if (fu_util_bios_setting_matches_args(setting, values)) {
			fu_util_bios_setting_update_description(setting);
			json_builder_begin_object(builder);
			fwupd_bios_setting_to_json(setting, builder);
			json_builder_end_object(builder);
		}
	}
	json_builder_end_array(builder);
	json_builder_end_object(builder);
	return fu_util_print_builder(builder, error);
}

gchar *
fu_util_bios_setting_to_string(FwupdBiosSetting *setting, guint idt)
{
	const gchar *tmp;
	FwupdBiosSettingKind type;
	g_autofree gchar *current_value = NULL;
	g_autoptr(GString) str = g_string_new(NULL);

	if (g_getenv("FWUPD_VERBOSE") != NULL) {
		g_autofree gchar *debug_str = NULL;
		debug_str = fwupd_bios_setting_to_string(setting);
		g_debug("%s", debug_str);
		return NULL;
	}
	tmp = fwupd_bios_setting_get_name(setting);
	fu_string_append(str, idt, tmp, NULL);

	type = fwupd_bios_setting_get_kind(setting);
	tmp = fu_util_bios_setting_kind_to_string(type);
	if (tmp != NULL) {
		/* TRANSLATORS: type of BIOS setting */
		fu_string_append(str, idt + 1, _("Setting type"), tmp);
	}

	tmp = fwupd_bios_setting_get_current_value(setting);
	if (tmp != NULL) {
		current_value = g_strdup(tmp);
	} else {
		/* TRANSLATORS: tell a user how to get information */
		current_value = g_strdup_printf(_("Run without '%s' to see"), "--no-authenticate");
	}
	/* TRANSLATORS: current value of a BIOS setting */
	fu_string_append(str, idt + 1, _("Current Value"), current_value);

	fu_util_bios_setting_update_description(setting);
	tmp = fwupd_bios_setting_get_description(setting);
	if (tmp != NULL) {
		/* TRANSLATORS: description of BIOS setting */
		fu_string_append(str, idt + 1, _("Description"), tmp);
	}

	if (fwupd_bios_setting_get_read_only(setting)) {
		/* TRANSLATORS: item is TRUE */
		tmp = _("True");
	} else {
		/* TRANSLATORS: item is FALSE */
		tmp = _("False");
	}
	/* TRANSLATORS: BIOS setting is read only */
	fu_string_append(str, idt + 1, _("Read Only"), tmp);

	if (type == FWUPD_BIOS_SETTING_KIND_INTEGER || type == FWUPD_BIOS_SETTING_KIND_STRING) {
		g_autofree gchar *lower =
		    g_strdup_printf("%" G_GUINT64_FORMAT,
				    fwupd_bios_setting_get_lower_bound(setting));
		g_autofree gchar *upper =
		    g_strdup_printf("%" G_GUINT64_FORMAT,
				    fwupd_bios_setting_get_upper_bound(setting));
		if (type == FWUPD_BIOS_SETTING_KIND_INTEGER) {
			g_autofree gchar *scalar =
			    g_strdup_printf("%" G_GUINT64_FORMAT,
					    fwupd_bios_setting_get_scalar_increment(setting));
			if (lower != NULL) {
				/* TRANSLATORS: Lowest valid integer for BIOS setting */
				fu_string_append(str, idt + 1, _("Minimum value"), lower);
			}
			if (upper != NULL) {
				/* TRANSLATORS: Highest valid integer for BIOS setting */
				fu_string_append(str, idt + 1, _("Maximum value"), upper);
			}
			if (scalar != NULL) {
				/* TRANSLATORS: Scalar increment for integer BIOS setting */
				fu_string_append(str, idt + 1, _("Scalar Increment"), scalar);
			}
		} else {
			if (lower != NULL) {
				/* TRANSLATORS: Shortest valid string for BIOS setting */
				fu_string_append(str, idt + 1, _("Minimum length"), lower);
			}
			if (upper != NULL) {
				/* TRANSLATORS: Longest valid string for BIOS setting */
				fu_string_append(str, idt + 1, _("Maximum length"), upper);
			}
		}
	} else if (type == FWUPD_BIOS_SETTING_KIND_ENUMERATION) {
		GPtrArray *values = fwupd_bios_setting_get_possible_values(setting);
		if (values != NULL && values->len > 0) {
			/* TRANSLATORS: Possible values for a bios setting */
			fu_string_append(str, idt + 1, _("Possible Values"), NULL);
			for (guint i = 0; i < values->len; i++) {
				const gchar *possible = g_ptr_array_index(values, i);
				g_autofree gchar *index = g_strdup_printf("%u", i);
				fu_string_append(str, idt + 2, index, possible);
			}
		}
	}
	return g_string_free(g_steal_pointer(&str), FALSE);
}

GHashTable *
fu_util_bios_settings_parse_argv(gchar **input, GError **error)
{
	GHashTable *bios_settings;

	/* json input */
	if (g_strv_length(input) == 1) {
		g_autoptr(FuBiosSettings) new_bios_settings = fu_bios_settings_new();

		if (!fu_bios_settings_from_json_file(new_bios_settings, input[0], error))
			return NULL;

		return fu_bios_settings_to_hash_kv(new_bios_settings);
	}

	if (g_strv_length(input) == 0 || g_strv_length(input) % 2) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INVALID_ARGS,
				    /* TRANSLATORS: error message */
				    _("Invalid arguments"));
		return NULL;
	}

	bios_settings = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, g_free);
	for (guint i = 0; i < g_strv_length(input); i += 2)
		g_hash_table_insert(bios_settings, g_strdup(input[i]), g_strdup(input[i + 1]));

	return bios_settings;
}
