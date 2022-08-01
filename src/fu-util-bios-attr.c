/*
 * Copyright (C) 2022 Mario Limonciello <mario.limonciello@amd.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#define G_LOG_DOMAIN "FuMain"

#include "config.h"

#include <glib/gi18n.h>

#include "fu-util-bios-attr.h"
#include "fu-util-common.h"

static gboolean
fu_util_bios_attr_dup_fields(const gchar *name, const gchar *desc)
{
	return g_strcmp0(name, desc) == 0;
}

static void
fu_util_bios_attr_update_description(FwupdBiosAttr *attr)
{
	const gchar *name = fwupd_bios_attr_get_name(attr);
	const gchar *old = fwupd_bios_attr_get_description(attr);
	const gchar *new = NULL;

	if (g_strcmp0(name, FWUPD_BIOS_ATTR_PENDING_REBOOT) == 0) {
		/* TRANSLATORS: Settings refers to BIOS settings in this context */
		new = _("Settings will apply after system reboots");
	}
	/* For providing a better description on a number of Lenovo systems */
	if (fu_util_bios_attr_dup_fields(name, old)) {
		if (g_strcmp0(old, "WindowsUEFIFirmwareUpdate") == 0) {
			/* TRANSLATORS: description of a BIOS setting */
			new = _("BIOS updates delivered via LVFS or Windows Update");
		}
	}
	if (new != NULL)
		fwupd_bios_attr_set_description(attr, new);
}

static const gchar *
fu_util_bios_attr_kind_to_string(FwupdBiosAttrKind kind)
{
	if (kind == FWUPD_BIOS_ATTR_KIND_ENUMERATION) {
		/* TRANSLATORS: The BIOS setting can only be changed to fixed values */
		return _("Enumeration");
	}
	if (kind == FWUPD_BIOS_ATTR_KIND_INTEGER) {
		/* TRANSLATORS: The BIOS setting only accepts integers in a fixed range */
		return _("Integer");
	}
	if (kind == FWUPD_BIOS_ATTR_KIND_STRING) {
		/* TRANSLATORS: The BIOS setting accepts strings */
		return _("String");
	}
	return NULL;
}

gboolean
fu_util_bios_attr_matches_args(FwupdBiosAttr *attr, gchar **values)
{
	const gchar *name;

	/* no arguments set */
	if (g_strv_length(values) == 0)
		return TRUE;
	name = fwupd_bios_attr_get_name(attr);

	/* check all arguments */
	for (guint j = 0; j < g_strv_length(values); j++) {
		if (g_strcmp0(name, values[j]) == 0)
			return TRUE;
	}
	return FALSE;
}

gboolean
fu_util_get_bios_attr_as_json(gchar **values, GPtrArray *attrs, GError **error)
{
	g_autoptr(JsonBuilder) builder = json_builder_new();
	json_builder_begin_object(builder);

	json_builder_set_member_name(builder, "BiosAttributes");
	json_builder_begin_array(builder);
	for (guint i = 0; i < attrs->len; i++) {
		FwupdBiosAttr *attr = g_ptr_array_index(attrs, i);
		if (fu_util_bios_attr_matches_args(attr, values)) {
			fu_util_bios_attr_update_description(attr);
			json_builder_begin_object(builder);
			fwupd_bios_attr_to_json(attr, builder);
			json_builder_end_object(builder);
		}
	}
	json_builder_end_array(builder);
	json_builder_end_object(builder);
	return fu_util_print_builder(builder, error);
}

gchar *
fu_util_bios_attr_to_string(FwupdBiosAttr *attr, guint idt)
{
	const gchar *tmp;
	FwupdBiosAttrKind type;
	g_autoptr(GError) error_local = NULL;
	g_autoptr(GString) str = g_string_new(NULL);

	if (g_getenv("FWUPD_VERBOSE") != NULL) {
		g_autofree gchar *debug_str = NULL;
		debug_str = fwupd_bios_attr_to_string(attr);
		g_debug("%s", debug_str);
		return NULL;
	}
	tmp = fwupd_bios_attr_get_name(attr);
	fu_string_append(str, idt, tmp, NULL);

	type = fwupd_bios_attr_get_kind(attr);
	tmp = fu_util_bios_attr_kind_to_string(type);
	if (tmp != NULL) {
		/* TRANSLATORS: type of BIOS setting */
		fu_string_append(str, idt + 1, _("Setting type"), tmp);
	}

	tmp = fwupd_bios_attr_get_current_value(attr);
	if (tmp != NULL) {
		/* TRANSLATORS: current value of a BIOS setting */
		fu_string_append(str, idt + 1, _("Current Value"), tmp);
	}

	fu_util_bios_attr_update_description(attr);
	tmp = fwupd_bios_attr_get_description(attr);
	if (tmp != NULL) {
		/* TRANSLATORS: description of BIOS setting */
		fu_string_append(str, idt + 1, _("Description"), tmp);
	}

	if (fwupd_bios_attr_get_read_only(attr)) {
		/* TRANSLATORS: item is TRUE */
		tmp = _("True");
	} else {
		/* TRANSLATORS: item is FALSE */
		tmp = _("False");
	}
	/* TRANSLATORS: BIOS setting is read only */
	fu_string_append(str, idt + 1, _("Read Only"), tmp);

	if (type == FWUPD_BIOS_ATTR_KIND_INTEGER || type == FWUPD_BIOS_ATTR_KIND_STRING) {
		g_autofree gchar *lower =
		    g_strdup_printf("%" G_GUINT64_FORMAT, fwupd_bios_attr_get_lower_bound(attr));
		g_autofree gchar *upper =
		    g_strdup_printf("%" G_GUINT64_FORMAT, fwupd_bios_attr_get_upper_bound(attr));
		if (type == FWUPD_BIOS_ATTR_KIND_INTEGER) {
			g_autofree gchar *scalar =
			    g_strdup_printf("%" G_GUINT64_FORMAT,
					    fwupd_bios_attr_get_scalar_increment(attr));
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
	} else if (type == FWUPD_BIOS_ATTR_KIND_ENUMERATION) {
		GPtrArray *values = fwupd_bios_attr_get_possible_values(attr);
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
