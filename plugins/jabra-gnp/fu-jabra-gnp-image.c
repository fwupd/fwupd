/*
 * Copyright (C) 2023 GN Audio A/S
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include <fwupdplugin.h>

#include "fu-jabra-gnp-common.h"
#include "fu-jabra-gnp-image.h"

struct _FuJabraGnpImage {
	FuArchiveFirmware parent_instance;
	guint32 crc32;
};

G_DEFINE_TYPE(FuJabraGnpImage, fu_jabra_gnp_image, FU_TYPE_FIRMWARE)

static void
fu_jabra_gnp_image_export(FuFirmware *firmware, FuFirmwareExportFlags flags, XbBuilderNode *bn)
{
	FuJabraGnpImage *self = FU_JABRA_GNP_IMAGE(firmware);
	fu_xmlb_builder_insert_kx(bn, "crc32", self->crc32);
}

gboolean
fu_jabra_gnp_image_parse(FuJabraGnpImage *self,
			 XbNode *n,
			 FuFirmware *firmware_archive,
			 GError **error)
{
	const gchar *crc_str = NULL;
	const gchar *language;
	const gchar *name;
	const gchar *part_str = NULL;
	guint64 crc_expected = 0;
	guint64 partition = 0;
	g_autoptr(FuFirmware) img_archive = NULL;
	g_autoptr(GBytes) blob = NULL;

	/* only match on US English */
	language = xb_node_query_text(n, "language", NULL);
	if (language == NULL) {
		g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA, "language missing");
		return FALSE;
	}
	if (g_strcmp0(language, "English") != 0) {
		g_set_error(error,
			    G_IO_ERROR,
			    G_IO_ERROR_INVALID_DATA,
			    "language was not 'English', got '%s'",
			    language);
		return FALSE;
	}

	/* get the CRC */
	crc_str = xb_node_query_text(n, "crc", NULL);
	if (crc_str == NULL) {
		g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA, "crc missing");
		return FALSE;
	}
	if (!fu_strtoull(crc_str, &crc_expected, 0x0, 0xFFFFFFFF, error)) {
		g_prefix_error(error, "cannot parse crc of %s: ", crc_str);
		return FALSE;
	}

	/* get the partition number */
	part_str = xb_node_query_text(n, "partition", NULL);
	if (part_str == NULL) {
		g_set_error_literal(error,
				    G_IO_ERROR,
				    G_IO_ERROR_INVALID_DATA,
				    "partition missing");
		return FALSE;
	}
	if (!fu_strtoull(part_str, &partition, 0x0, 0xFFFFFFFF, error)) {
		g_prefix_error(error, "cannot parse partition of %s: ", part_str);
		return FALSE;
	}
	fu_firmware_set_idx(FU_FIRMWARE(self), partition);

	/* get the file pointed to by 'name' */
	name = xb_node_get_attr(n, "name");
	if (name == NULL) {
		g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA, "name missing");
		return FALSE;
	}
	fu_firmware_set_id(FU_FIRMWARE(self), name);
	img_archive = fu_archive_firmware_get_image_fnmatch(FU_ARCHIVE_FIRMWARE(firmware_archive),
							    name,
							    error);
	if (img_archive == NULL)
		return FALSE;
	blob = fu_firmware_get_bytes(img_archive, error);
	if (blob == NULL)
		return FALSE;

	/* verify the CRC */
	self->crc32 = fu_jabra_gnp_calculate_crc(blob);
	if (self->crc32 != (guint32)crc_expected) {
		g_set_error(error,
			    G_IO_ERROR,
			    G_IO_ERROR_INVALID_DATA,
			    "checksum invalid, got 0x%x, expected 0x%x",
			    (guint)self->crc32,
			    (guint)crc_expected);
		return FALSE;
	}

	/* success */
	fu_firmware_set_bytes(FU_FIRMWARE(self), blob);
	return TRUE;
}

guint32
fu_jabra_gnp_image_get_crc32(FuJabraGnpImage *self)
{
	g_return_val_if_fail(FU_IS_JABRA_GNP_IMAGE(self), G_MAXUINT32);
	return self->crc32;
}

static void
fu_jabra_gnp_image_init(FuJabraGnpImage *self)
{
	fu_firmware_add_flag(FU_FIRMWARE(self), FU_FIRMWARE_FLAG_HAS_CHECKSUM);
}

static void
fu_jabra_gnp_image_class_init(FuJabraGnpImageClass *klass)
{
	FuFirmwareClass *klass_firmware = FU_FIRMWARE_CLASS(klass);
	klass_firmware->export = fu_jabra_gnp_image_export;
}

FuJabraGnpImage *
fu_jabra_gnp_image_new(void)
{
	return FU_JABRA_GNP_IMAGE(g_object_new(FU_TYPE_JABRA_GNP_IMAGE, NULL));
}
