/*
 * Copyright 2026 Advanced Micro Devices, Inc.
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include "fu-amd-afc-option.h"
#include "fu-amd-afc-setting.h"

void
fu_amd_afc_setting_export(FuAmdAfcSetting *self,
			  FuFirmwareExportFlags flags,
			  FwupdJsonObject *json_object)
{
	const gchar *kind_str = fu_amd_afc_setting_kind_to_string(self->kind);
	if (kind_str != NULL)
		fwupd_json_object_add_string(json_object, "kind", kind_str);
	if (self->display_name != NULL)
		fwupd_json_object_add_string(json_object, "display_name", self->display_name);
	if (self->language != NULL)
		fwupd_json_object_add_string(json_object, "language", self->language);
	if (self->paths->len > 0) {
		g_autoptr(FwupdJsonArray) json_paths = fwupd_json_array_new();
		for (guint i = 0; i < self->paths->len; i++) {
			const gchar *path = g_ptr_array_index(self->paths, i);
			if (path != NULL)
				fwupd_json_array_add_string(json_paths, path);
		}
		fwupd_json_object_add_array(json_object, "paths", json_paths);
	}
	if (self->options->len > 0) {
		g_autoptr(FwupdJsonArray) json_options = fwupd_json_array_new();
		for (guint i = 0; i < self->options->len; i++) {
			FuAmdAfcOption *option = g_ptr_array_index(self->options, i);
			g_autoptr(FwupdJsonObject) json_option = fwupd_json_object_new();
			fu_amd_afc_option_export(option, flags, json_option);
			fwupd_json_array_add_object(json_options, json_option);
		}
		fwupd_json_object_add_array(json_object, "options", json_options);
	}
	if (self->question_id != 0)
		fwupd_json_object_add_integer(json_object, "question_id", self->question_id);
	if (self->question_flags != 0)
		fwupd_json_object_add_integer(json_object, "question_flags", self->question_flags);
	fwupd_json_object_add_boolean(json_object, "has_current", self->has_current);
	if (self->current != 0)
		fwupd_json_object_add_integer(json_object, "current", (gint64)self->current);
	if (self->minimum != 0)
		fwupd_json_object_add_integer(json_object, "minimum", (gint64)self->minimum);
	if (self->maximum != 0)
		fwupd_json_object_add_integer(json_object, "maximum", (gint64)self->maximum);
	if (self->step != 0)
		fwupd_json_object_add_integer(json_object, "step", (gint64)self->step);
	fwupd_json_object_add_boolean(json_object, "has_default", self->has_default);
	if (self->default_value != 0)
		fwupd_json_object_add_integer(json_object,
					      "default_value",
					      (gint64)self->default_value);
}

void
fu_amd_afc_setting_free(FuAmdAfcSetting *self)
{
	if (self->paths != NULL)
		g_ptr_array_unref(self->paths);
	if (self->options != NULL)
		g_ptr_array_unref(self->options);
	g_free(self->display_name);
	g_free(self->language);
	g_free(self);
}

const gchar *
fu_amd_afc_setting_get_option_name(FuAmdAfcSetting *self, guint64 value)
{
	for (guint i = 0; i < self->options->len; i++) {
		FuAmdAfcOption *option = g_ptr_array_index(self->options, i);
		if (option->value == value)
			return option->name;
	}
	return NULL;
}

gchar *
fu_amd_afc_setting_build_name(FuAmdAfcSetting *self)
{
	g_autoptr(GString) name = g_string_new(NULL);
	g_autofree gchar *component = NULL;
	for (guint i = 0; i < self->paths->len; i++) {
		const gchar *path = g_ptr_array_index(self->paths, i);
		g_set_str(&component, path);
		g_strstrip(component);
		if (component[0] == '\0')
			continue;
		if (name->len > 0)
			g_string_append_c(name, '|');
		for (const gchar *p = component; *p != '\0'; p++)
			g_string_append_c(name, *p == '/' ? '!' : *p);
	}
	return g_string_free_and_steal(g_steal_pointer(&name));
}
