/*
 * Copyright 2026 Advanced Micro Devices, Inc.
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include "fu-amd-afc-option.h"
#include "fu-amd-afc-setting.h"

void
fu_amd_afc_setting_export(FuAmdAfcSetting *self, FuFirmwareExportFlags flags, XbBuilderNode *bn)
{
	fu_xmlb_builder_insert_kv(bn, "kind", fu_amd_afc_setting_kind_to_string(self->kind));
	if (self->display_name != NULL)
		fu_xmlb_builder_insert_kv(bn, "display_name", self->display_name);
	if (self->language != NULL)
		fu_xmlb_builder_insert_kv(bn, "language", self->language);
	if (self->paths->len > 0) {
		g_autoptr(XbBuilderNode) bc = xb_builder_node_insert(bn, "paths", NULL);
		for (guint i = 0; i < self->paths->len; i++) {
			const gchar *path = g_ptr_array_index(self->paths, i);
			fu_xmlb_builder_insert_kv(bc, "path", path);
		}
	}
	if (self->options->len > 0) {
		g_autoptr(XbBuilderNode) bc = xb_builder_node_insert(bn, "options", NULL);
		for (guint i = 0; i < self->options->len; i++) {
			FuAmdAfcOption *option = g_ptr_array_index(self->options, i);
			g_autoptr(XbBuilderNode) bcc = xb_builder_node_insert(bc, "option", NULL);
			fu_amd_afc_option_export(option, flags, bcc);
		}
	}
	fu_xmlb_builder_insert_kx(bn, "question_id", self->question_id);
	fu_xmlb_builder_insert_kx(bn, "question_flags", self->question_flags);
	fu_xmlb_builder_insert_kb(bn, "has_current", self->has_current);
	fu_xmlb_builder_insert_kx(bn, "current", self->current);
	fu_xmlb_builder_insert_kx(bn, "minimum", self->minimum);
	fu_xmlb_builder_insert_kx(bn, "maximum", self->maximum);
	fu_xmlb_builder_insert_kx(bn, "step", self->step);
	fu_xmlb_builder_insert_kb(bn, "has_default", self->has_default);
	fu_xmlb_builder_insert_kx(bn, "default_value", self->default_value);
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
		g_clear_pointer(&component, g_free);
		component = g_strdup(g_ptr_array_index(self->paths, i));
		g_strstrip(component);
		if (component[0] == '\0')
			continue;
		if (name->len > 0)
			g_string_append_c(name, '|');
		for (const gchar *p = component; *p != '\0'; p++)
			g_string_append_c(name, *p == '/' ? '!' : *p);
	}
	return g_string_free(g_steal_pointer(&name), FALSE);
}
