/*
 * Copyright 2026 Advanced Micro Devices, Inc.
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include "fu-amd-afc-plugin.h"
#include "fu-amd-afc.h"

struct _FuAmdAfcPlugin {
	FuPlugin parent_instance;
	FuAmdAfcState *state;
};

G_DEFINE_TYPE(FuAmdAfcPlugin, fu_amd_afc_plugin, FU_TYPE_PLUGIN)

static gboolean
fu_amd_afc_plugin_startup(FuPlugin *plugin, FuProgress *progress, GError **error)
{
	FuAmdAfcPlugin *self = FU_AMD_AFC_PLUGIN(plugin);
	FuContext *ctx = fu_plugin_get_context(plugin);
	g_autoptr(FuBiosSettings) bios_settings = fu_context_get_bios_settings(ctx);
	g_autoptr(GDir) dir = NULL;
	g_autofree gchar *tables_dir = NULL;
	guint parsed = 0;

	if (fu_context_get_cpu_vendor(ctx) != FU_CPU_VENDOR_AMD) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_NOT_SUPPORTED,
				    "AFC requires an AMD platform");
		return FALSE;
	}
	if (!fu_efivars_supported(fu_context_get_efivars(ctx), error))
		return FALSE;
	tables_dir = fu_context_build_filename(ctx, error, FU_PATH_KIND_ACPI_TABLES, NULL);
	if (tables_dir == NULL)
		return FALSE;
	dir = g_dir_open(tables_dir, 0, error);
	if (dir == NULL) {
		fwupd_error_convert(error);
		return FALSE;
	}
	self->state = fu_amd_afc_state_new(ctx);
	while (TRUE) {
		const gchar *basename = g_dir_read_name(dir);
		g_autofree gchar *filename = NULL;
		g_autofree gchar *contents = NULL;
		gsize contents_sz = 0;
		g_autoptr(GBytes) bytes = NULL;
		if (basename == NULL)
			break;
		if (!g_str_has_prefix(basename, "SSDT"))
			continue;
		filename = g_build_filename(tables_dir, basename, NULL);
		if (!g_file_get_contents(filename, &contents, &contents_sz, error)) {
			fwupd_error_convert(error);
			return FALSE;
		}
		bytes = g_bytes_new_take(g_steal_pointer(&contents), contents_sz);
		if (!fu_amd_afc_state_parse_table(self->state, bytes, error)) {
			if (g_error_matches(*error, FWUPD_ERROR, FWUPD_ERROR_NOT_SUPPORTED)) {
				g_clear_error(error);
				continue;
			}
			g_prefix_error(error, "failed to parse %s: ", basename);
			return FALSE;
		}
		parsed++;
	}
	if (parsed == 0 || fu_amd_afc_state_get_setting_count(self->state) == 0) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_NOT_SUPPORTED,
				    "no AFC firmware configuration found");
		return FALSE;
	}
	if (!fu_amd_afc_state_add_bios_settings(self->state, bios_settings, error))
		return FALSE;
	g_info("loaded %u AFC tables with %u BIOS settings",
	       parsed,
	       fu_amd_afc_state_get_setting_count(self->state));
	return TRUE;
}

static void
fu_amd_afc_plugin_finalize(GObject *object)
{
	FuAmdAfcPlugin *self = FU_AMD_AFC_PLUGIN(object);
	if (self->state != NULL)
		fu_amd_afc_state_unref(self->state);
	G_OBJECT_CLASS(fu_amd_afc_plugin_parent_class)->finalize(object);
}

static void
fu_amd_afc_plugin_class_init(FuAmdAfcPluginClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS(klass);
	FuPluginClass *plugin_class = FU_PLUGIN_CLASS(klass);
	object_class->finalize = fu_amd_afc_plugin_finalize;
	plugin_class->startup = fu_amd_afc_plugin_startup;
}

static void
fu_amd_afc_plugin_init(FuAmdAfcPlugin *self)
{
}
