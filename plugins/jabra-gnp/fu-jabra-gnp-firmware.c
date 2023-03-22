/*
 * Copyright (C) 2023 GN Audio A/S
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include <fwupdplugin.h>

#include "fu-jabra-gnp-firmware.h"
#include "fu-jabra-gnp-image.h"

struct _FuJabraGnpFirmware {
	FuArchiveFirmware parent_instance;
	guint16 dfu_pid;
	FuJabraGnpVersionData version_data;
};

G_DEFINE_TYPE(FuJabraGnpFirmware, fu_jabra_gnp_firmware, FU_TYPE_FIRMWARE)

static void
fu_jabra_gnp_firmware_export(FuFirmware *firmware, FuFirmwareExportFlags flags, XbBuilderNode *bn)
{
	FuJabraGnpFirmware *self = FU_JABRA_GNP_FIRMWARE(firmware);
	fu_xmlb_builder_insert_kx(bn, "dfu_pid", self->dfu_pid);
}

static gboolean
fu_jabra_gnp_firmware_parse_version(FuJabraGnpFirmware *self, GError **error)
{
	guint64 val = 0;
	g_auto(GStrv) split = NULL;

	split = g_strsplit(fu_firmware_get_version(FU_FIRMWARE(self)), ".", -1);
	if (g_strv_length(split) != 3) {
		g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA, "version invalid");
		return FALSE;
	}
	if (!fu_strtoull(split[0], &val, 0x0, 0xFF, error))
		return FALSE;
	self->version_data.major = (guint8)val;
	if (!fu_strtoull(split[1], &val, 0x0, 0xFF, error))
		return FALSE;
	self->version_data.minor = (guint8)val;
	if (!fu_strtoull(split[2], &val, 0x0, 0xFF, error))
		return FALSE;
	self->version_data.micro = (guint8)val;

	/* success */
	return TRUE;
}

static gboolean
fu_jabra_gnp_firmware_parse_info(FuJabraGnpFirmware *self, XbSilo *silo, GError **error)
{
	const gchar *version;
	const gchar *dfu_pid_str;
	guint64 val = 0;
	g_autoptr(XbNode) dfu_pid = NULL;
	g_autoptr(XbNode) build_vector = NULL;

	build_vector = xb_silo_query_first(silo, "buildVector", error);
	if (build_vector == NULL)
		return FALSE;

	/* only first? */
	version = xb_node_get_attr(build_vector, "version");
	if (version == NULL) {
		g_set_error_literal(error,
				    G_IO_ERROR,
				    G_IO_ERROR_INVALID_DATA,
				    "buildVector version missing");
		return FALSE;
	}
	fu_firmware_set_version(FU_FIRMWARE(self), version);
	if (!fu_jabra_gnp_firmware_parse_version(self, error))
		return FALSE;
	dfu_pid = xb_silo_query_first(silo, "buildVector/targetUsbPids", error);
	if (dfu_pid == NULL)
		return FALSE;
	dfu_pid_str = xb_node_query_text(dfu_pid, "usbPid", error);
	if (dfu_pid_str == NULL)
		return FALSE;
	if (!fu_strtoull(dfu_pid_str, &val, 0x0, 0xFFFF, error)) {
		g_prefix_error(error, "cannot parse usbPid of %s: ", dfu_pid_str);
		return FALSE;
	}
	self->dfu_pid = (guint16)val;

	/* success */
	return TRUE;
}

static gboolean
fu_jabra_gnp_firmware_parse(FuFirmware *firmware,
			    GBytes *fw,
			    gsize offset,
			    FwupdInstallFlags flags,
			    GError **error)
{
	FuJabraGnpFirmware *self = FU_JABRA_GNP_FIRMWARE(firmware);
	g_autoptr(FuFirmware) firmware_archive = fu_archive_firmware_new();
	g_autoptr(FuFirmware) img_xml = NULL;
	g_autoptr(GBytes) img_blob = NULL;
	g_autoptr(GPtrArray) files = NULL;
	g_autoptr(XbBuilder) builder = xb_builder_new();
	g_autoptr(XbBuilderSource) source = xb_builder_source_new();
	g_autoptr(XbSilo) silo = NULL;

	/* FuArchiveFirmware->parse */
	fu_archive_firmware_set_format(FU_ARCHIVE_FIRMWARE(firmware_archive),
				       FU_ARCHIVE_FORMAT_ZIP);
	fu_archive_firmware_set_compression(FU_ARCHIVE_FIRMWARE(firmware_archive),
					    FU_ARCHIVE_COMPRESSION_NONE);
	if (!fu_firmware_parse_full(firmware_archive, fw, offset, flags, error))
		return FALSE;

	/* parse the XML metadata */
	img_xml = fu_archive_firmware_get_image_fnmatch(FU_ARCHIVE_FIRMWARE(firmware_archive),
							"info.xml",
							error);
	if (img_xml == NULL)
		return FALSE;
	img_blob = fu_firmware_get_bytes(img_xml, error);
	if (img_blob == NULL)
		return FALSE;
	if (!xb_builder_source_load_bytes(source, img_blob, XB_BUILDER_SOURCE_FLAG_NONE, error))
		return FALSE;
	xb_builder_import_source(builder, source);
	silo = xb_builder_compile(builder, XB_BUILDER_COMPILE_FLAG_NONE, NULL, error);
	if (silo == NULL)
		return FALSE;
	if (!fu_jabra_gnp_firmware_parse_info(self, silo, error))
		return FALSE;
	files = xb_silo_query(silo, "buildVector/files/file", 0, error);
	if (files == NULL)
		return FALSE;
	for (guint i = 0; i < files->len; i++) {
		XbNode *n = g_ptr_array_index(files, i);
		g_autoptr(FuJabraGnpImage) img = fu_jabra_gnp_image_new();
		g_autoptr(GError) error_local = NULL;
		if (!fu_jabra_gnp_image_parse(img, n, firmware_archive, &error_local)) {
			if (g_error_matches(error_local, G_IO_ERROR, G_IO_ERROR_INVALID_DATA)) {
				g_debug("ignoring image 0x%x: %s", i, error_local->message);
				continue;
			}
			g_propagate_error(error, g_steal_pointer(&error_local));
			return FALSE;
		}
		fu_firmware_add_image(firmware, FU_FIRMWARE(img));
	}

	/* success */
	return TRUE;
}

guint16
fu_jabra_gnp_firmware_get_dfu_pid(FuJabraGnpFirmware *self)
{
	g_return_val_if_fail(FU_IS_JABRA_GNP_FIRMWARE(self), G_MAXUINT16);
	return self->dfu_pid;
}

FuJabraGnpVersionData *
fu_jabra_gnp_firmware_get_version_data(FuJabraGnpFirmware *self)
{
	g_return_val_if_fail(FU_IS_JABRA_GNP_FIRMWARE(self), NULL);
	return &self->version_data;
}

static void
fu_jabra_gnp_firmware_init(FuJabraGnpFirmware *self)
{
	fu_firmware_add_flag(FU_FIRMWARE(self), FU_FIRMWARE_FLAG_HAS_VID_PID);
}

static void
fu_jabra_gnp_firmware_class_init(FuJabraGnpFirmwareClass *klass)
{
	FuFirmwareClass *klass_firmware = FU_FIRMWARE_CLASS(klass);
	klass_firmware->parse = fu_jabra_gnp_firmware_parse;
	klass_firmware->export = fu_jabra_gnp_firmware_export;
}

FuFirmware *
fu_jabra_gnp_firmware_new(void)
{
	return FU_FIRMWARE(g_object_new(FU_TYPE_JABRA_GNP_FIRMWARE, NULL));
}
