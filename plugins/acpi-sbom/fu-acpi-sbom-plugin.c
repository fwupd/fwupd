/*
 * Copyright 2026 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include "fu-acpi-sbom-entry.h"
#include "fu-acpi-sbom-plugin.h"
#include "fu-acpi-sbom-table.h"

struct _FuAcpiSbomPlugin {
	FuPlugin parent_instance;
};

G_DEFINE_TYPE(FuAcpiSbomPlugin, fu_acpi_sbom_plugin, FU_TYPE_PLUGIN)

static void
fu_acpi_sbom_plugin_init(FuAcpiSbomPlugin *self)
{
}

static const gchar *
fu_acpi_sbom_plugin_uswid_format_get_ext(FuUswidPayloadFormat format)
{
	if (format == FU_USWID_PAYLOAD_FORMAT_COSWID)
		return "coswid";
	if (format == FU_USWID_PAYLOAD_FORMAT_SPDX)
		return "spdx.json";
	if (format == FU_USWID_PAYLOAD_FORMAT_CYCLONEDX)
		return "cdx.json";
	return "raw";
}

static gboolean
fu_acpi_sbom_plugin_coldplug_entry(FuAcpiSbomPlugin *self, FuFirmware *entry, GError **error)
{
	FuContext *ctx = fu_plugin_get_context(FU_PLUGIN(self));
	const gchar *format_ext;
	g_autofree gchar *basename = NULL;
	g_autofree gchar *checksum = NULL;
	g_autofree gchar *filename = NULL;

	checksum = fu_firmware_get_checksum(entry, G_CHECKSUM_SHA1, error);
	if (checksum == NULL)
		return FALSE;
	format_ext = fu_acpi_sbom_plugin_uswid_format_get_ext(
	    fu_acpi_sbom_entry_get_format(FU_ACPI_SBOM_ENTRY(entry)));
	basename = g_strdup_printf("%s.%s", checksum, format_ext);
	filename = fu_context_build_filename(ctx,
					     error,
					     FU_PATH_KIND_LOCALSTATEDIR_PKG,
					     "sbom",
					     basename,
					     NULL);
	if (filename == NULL)
		return FALSE;
	if (!g_file_test(filename, G_FILE_TEST_EXISTS)) {
		g_autoptr(GBytes) blob = NULL;
		blob = fu_firmware_get_bytes(entry, error);
		if (blob == NULL)
			return FALSE;
		if (!fu_path_mkdir_parent(filename, error))
			return FALSE;
		if (!fu_bytes_set_contents(filename, blob, error))
			return FALSE;
	}

	/* success */
	return TRUE;
}

static gboolean
fu_acpi_sbom_plugin_coldplug(FuPlugin *plugin, FuProgress *progress, GError **error)
{
	FuAcpiSbomPlugin *self = FU_ACPI_SBOM_PLUGIN(plugin);
	FuContext *ctx = fu_plugin_get_context(plugin);
	g_autofree gchar *fn = NULL;
	g_autoptr(FuFirmware) fw_sbom = fu_acpi_sbom_table_new();
	g_autoptr(GBytes) blob = NULL;
	g_autoptr(GError) error_local = NULL;
	g_autoptr(GPtrArray) entries = NULL;

	fn = fu_context_build_filename(ctx, error, FU_PATH_KIND_ACPI_TABLES, "SBOM", NULL);
	if (fn == NULL)
		return FALSE;
	blob = fu_bytes_get_contents(fn, &error_local);
	if (blob == NULL) {
		if (g_error_matches(error_local, FWUPD_ERROR, FWUPD_ERROR_INVALID_FILE)) {
			g_set_error_literal(error,
					    FWUPD_ERROR,
					    FWUPD_ERROR_NOT_SUPPORTED,
					    error_local->message);
			return FALSE;
		}
		g_propagate_error(error, g_steal_pointer(&error_local));
		return FALSE;
	}
	if (!fu_firmware_parse_bytes(fw_sbom, blob, 0x0, FU_FIRMWARE_PARSE_FLAG_NO_SEARCH, error))
		return FALSE;

	/* save to disk if they do not already exist */
	entries = fu_firmware_get_images(fw_sbom);
	for (guint i = 0; i < entries->len; i++) {
		FuFirmware *entry = g_ptr_array_index(entries, i);
		if (!fu_acpi_sbom_plugin_coldplug_entry(self, entry, error))
			return FALSE;
	}

	/* success */
	return TRUE;
}

static void
fu_acpi_sbom_plugin_constructed(GObject *obj)
{
	FuPlugin *plugin = FU_PLUGIN(obj);
	fu_plugin_add_firmware_gtype(plugin, FU_TYPE_ACPI_SBOM_TABLE);
	fu_plugin_add_firmware_gtype(plugin, FU_TYPE_ACPI_SBOM_ENTRY); /* coverage */

	/* chain up to parent */
	G_OBJECT_CLASS(fu_acpi_sbom_plugin_parent_class)->constructed(obj);
}

static void
fu_acpi_sbom_plugin_class_init(FuAcpiSbomPluginClass *klass)
{
	FuPluginClass *plugin_class = FU_PLUGIN_CLASS(klass);
	plugin_class->constructed = fu_acpi_sbom_plugin_constructed;
	plugin_class->coldplug = fu_acpi_sbom_plugin_coldplug;
}
