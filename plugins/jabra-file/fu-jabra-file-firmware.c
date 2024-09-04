/*
 * Copyright 2023 GN Audio A/S
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include <fwupdplugin.h>

#include "fu-jabra-file-firmware.h"

struct _FuJabraFileFirmware {
	FuArchiveFirmware parent_instance;
	guint16 dfu_pid;
};

G_DEFINE_TYPE(FuJabraFileFirmware, fu_jabra_file_firmware, FU_TYPE_FIRMWARE)

static void
fu_jabra_file_firmware_export(FuFirmware *firmware, FuFirmwareExportFlags flags, XbBuilderNode *bn)
{
	FuJabraFileFirmware *self = FU_JABRA_FILE_FIRMWARE(firmware);
	fu_xmlb_builder_insert_kx(bn, "dfu_pid", self->dfu_pid);
}

static gboolean
fu_jabra_file_firmware_parse_info(FuJabraFileFirmware *self, XbSilo *silo, GError **error)
{
	const gchar *version;
	const gchar *dfu_pid_str;
	guint64 val = 0;
	g_autoptr(XbNode) dfu_pid = NULL;
	g_autoptr(XbNode) build_vector = NULL;

	build_vector = xb_silo_query_first(silo, "buildVector", error);
	if (build_vector == NULL)
		return FALSE;

	version = xb_node_get_attr(build_vector, "version");
	if (version == NULL) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INVALID_DATA,
				    "buildVector version missing");
		return FALSE;
	}
	fu_firmware_set_version(FU_FIRMWARE(self), version);
	dfu_pid = xb_silo_query_first(silo, "buildVector/targetUsbPids", error);
	if (dfu_pid == NULL)
		return FALSE;
	dfu_pid_str = xb_node_query_text(dfu_pid, "usbPid", error);
	if (dfu_pid_str == NULL)
		return FALSE;
	if (!fu_strtoull(dfu_pid_str, &val, 0x0, G_MAXUINT16, FU_INTEGER_BASE_AUTO, error)) {
		g_prefix_error(error, "cannot parse usbPid of %s: ", dfu_pid_str);
		return FALSE;
	}
	self->dfu_pid = (guint16)val;

	/* success */
	return TRUE;
}

static gboolean
fu_jabra_file_firmware_parse(FuFirmware *firmware,
			     GInputStream *stream,
			     gsize offset,
			     FwupdInstallFlags flags,
			     GError **error)
{
	FuJabraFileFirmware *self = FU_JABRA_FILE_FIRMWARE(firmware);
	g_autoptr(FuFirmware) firmware_archive = fu_archive_firmware_new();
	g_autoptr(FuFirmware) img_xml = NULL;
	g_autoptr(FuFirmware) upgrade_archive = NULL;
	g_autoptr(GBytes) img_blob = NULL;
	g_autoptr(GBytes) upgrade_blob = NULL;
	g_autoptr(GPtrArray) files = NULL;
	g_autoptr(XbBuilder) builder = xb_builder_new();
	g_autoptr(XbBuilderSource) source = xb_builder_source_new();
	g_autoptr(XbSilo) silo = NULL;

	/* FuArchiveFirmware->parse */
	fu_archive_firmware_set_format(FU_ARCHIVE_FIRMWARE(firmware_archive),
				       FU_ARCHIVE_FORMAT_ZIP);
	fu_archive_firmware_set_compression(FU_ARCHIVE_FIRMWARE(firmware_archive),
					    FU_ARCHIVE_COMPRESSION_NONE);
	if (!fu_firmware_parse_stream(firmware_archive, stream, offset, flags, error))
		return FALSE;

	img_xml = fu_archive_firmware_get_image_fnmatch(FU_ARCHIVE_FIRMWARE(firmware_archive),
							"info.xml",
							error);
	img_blob = fu_firmware_get_bytes(img_xml, error);
	if (img_blob == NULL)
		return FALSE;
	if (!xb_builder_source_load_bytes(source, img_blob, XB_BUILDER_SOURCE_FLAG_NONE, error))
		return FALSE;
	xb_builder_import_source(builder, source);
	silo = xb_builder_compile(builder, XB_BUILDER_COMPILE_FLAG_NONE, NULL, error);
	if (silo == NULL)
		return FALSE;
	if (!fu_jabra_file_firmware_parse_info(self, silo, error))
		return FALSE;

	upgrade_archive =
	    fu_archive_firmware_get_image_fnmatch(FU_ARCHIVE_FIRMWARE(firmware_archive),
						  "upgrade.zip",
						  error);
	if (upgrade_archive == NULL)
		return FALSE;
	upgrade_blob = fu_firmware_get_bytes(upgrade_archive, error);
	if (upgrade_blob == NULL)
		return FALSE;

	fu_firmware_set_bytes(FU_FIRMWARE(self), upgrade_blob);
	/* success */
	return TRUE;
}

guint16
fu_jabra_file_firmware_get_dfu_pid(FuJabraFileFirmware *self)
{
	g_return_val_if_fail(FU_IS_JABRA_FILE_FIRMWARE(self), G_MAXUINT16);
	return self->dfu_pid;
}

static void
fu_jabra_file_firmware_init(FuJabraFileFirmware *self)
{
	fu_firmware_add_flag(FU_FIRMWARE(self), FU_FIRMWARE_FLAG_HAS_VID_PID);
	fu_firmware_set_images_max(FU_FIRMWARE(self), 1024);
}

static void
fu_jabra_file_firmware_class_init(FuJabraFileFirmwareClass *klass)
{
	FuFirmwareClass *firmware_class = FU_FIRMWARE_CLASS(klass);
	firmware_class->parse = fu_jabra_file_firmware_parse;
	firmware_class->export = fu_jabra_file_firmware_export;
}

FuFirmware *
fu_jabra_file_firmware_new(void)
{
	return FU_FIRMWARE(g_object_new(FU_TYPE_JABRA_FILE_FIRMWARE, NULL));
}
