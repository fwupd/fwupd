/*
 * Copyright 2022 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#define G_LOG_DOMAIN "FuFirmware"

#include "config.h"

#include "fu-bytes.h"
#include "fu-common.h"
#include "fu-crc.h"
#include "fu-dump.h"
#include "fu-fdt-image.h"
#include "fu-fit-firmware.h"
#include "fu-input-stream.h"
#include "fu-mem.h"

/**
 * FuFitFirmware:
 *
 * A Flat Image Tree.
 *
 * Documented:
 * https://github.com/u-boot/u-boot/blob/master/doc/uImage.FIT/source_file_format.txt
 *
 * See also: [class@FuFdtFirmware]
 */

G_DEFINE_TYPE(FuFitFirmware, fu_fit_firmware, FU_TYPE_FDT_FIRMWARE)

static FuFdtImage *
fu_fit_firmware_get_image_root(FuFitFirmware *self)
{
	FuFirmware *img = fu_firmware_get_image_by_id(FU_FIRMWARE(self), NULL, NULL);
	if (img != NULL)
		return FU_FDT_IMAGE(img);
	img = fu_fdt_image_new();
	fu_fdt_image_set_attr_uint32(FU_FDT_IMAGE(img), FU_FIT_FIRMWARE_ATTR_TIMESTAMP, 0x0);
	fu_fdt_image_set_attr_str(FU_FDT_IMAGE(img), "description", "Firmware image");
	fu_fdt_image_set_attr_str(FU_FDT_IMAGE(img), "creator", "fwupd");
	fu_firmware_add_image(FU_FIRMWARE(self), img);
	return FU_FDT_IMAGE(img);
}

/**
 * fu_fit_firmware_get_timestamp:
 * @self: a #FuFitFirmware
 *
 * Gets the creation timestamp.
 *
 * Returns: integer
 *
 * Since: 1.8.2
 **/
guint32
fu_fit_firmware_get_timestamp(FuFitFirmware *self)
{
	guint32 tmp = 0;
	g_autoptr(FuFdtImage) img_root = fu_fit_firmware_get_image_root(self);

	g_return_val_if_fail(FU_IS_FIT_FIRMWARE(self), 0x0);

	/* this has to exist */
	(void)fu_fdt_image_get_attr_u32(img_root, FU_FIT_FIRMWARE_ATTR_TIMESTAMP, &tmp, NULL);
	return tmp;
}

/**
 * fu_fit_firmware_set_timestamp:
 * @self: a #FuFitFirmware
 * @timestamp: integer value
 *
 * Sets the creation timestamp.
 *
 * Since: 1.8.2
 **/
void
fu_fit_firmware_set_timestamp(FuFitFirmware *self, guint32 timestamp)
{
	g_autoptr(FuFdtImage) img_root = fu_fit_firmware_get_image_root(self);
	g_return_if_fail(FU_IS_FIT_FIRMWARE(self));
	fu_fdt_image_set_attr_uint32(img_root, FU_FIT_FIRMWARE_ATTR_TIMESTAMP, timestamp);
}

static gboolean
fu_fit_firmware_verify_crc32(FuFirmware *firmware,
			     FuFirmware *img,
			     FuFirmware *img_hash,
			     GBytes *blob,
			     GError **error)
{
	guint32 value = 0;
	guint32 value_calc;

	/* get value and verify */
	if (!fu_fdt_image_get_attr_u32(FU_FDT_IMAGE(img_hash),
				       FU_FIT_FIRMWARE_ATTR_VALUE,
				       &value,
				       error))
		return FALSE;
	value_calc = fu_crc32(FU_CRC_KIND_B32_STANDARD,
			      g_bytes_get_data(blob, NULL),
			      g_bytes_get_size(blob));
	if (value_calc != value) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INVALID_DATA,
			    "%s CRC did not match, got 0x%x, expected 0x%x",
			    fu_firmware_get_id(img),
			    value,
			    value_calc);
		return FALSE;
	}

	/* success */
	fu_firmware_add_flag(firmware, FU_FIRMWARE_FLAG_HAS_CHECKSUM);
	return TRUE;
}

static gboolean
fu_fit_firmware_verify_checksum(FuFirmware *firmware,
				FuFirmware *img,
				FuFirmware *img_hash,
				GChecksumType checksum_type,
				GBytes *blob,
				GError **error)
{
	gsize digest_len = g_checksum_type_get_length(checksum_type);
	g_autofree guint8 *buf = g_malloc0(digest_len);
	g_autoptr(GBytes) value = NULL;
	g_autoptr(GBytes) value_calc = NULL;
	g_autoptr(GChecksum) checksum = g_checksum_new(checksum_type);

	/* get value and verify */
	value = fu_fdt_image_get_attr(FU_FDT_IMAGE(img_hash), FU_FIT_FIRMWARE_ATTR_VALUE, error);
	if (value == NULL)
		return FALSE;
	if (g_bytes_get_size(value) != digest_len) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INVALID_DATA,
			    "%s invalid hash value size, got 0x%x, expected 0x%x",
			    fu_firmware_get_id(img),
			    (guint)g_bytes_get_size(value),
			    (guint)digest_len);
		return FALSE;
	}
	g_checksum_update(checksum,
			  (const guchar *)g_bytes_get_data(value, NULL),
			  g_bytes_get_size(value));
	g_checksum_get_digest(checksum, buf, &digest_len);
	value_calc = g_bytes_new(buf, digest_len);
	if (!fu_bytes_compare(value, value_calc, error))
		return FALSE;

	/* success */
	fu_firmware_add_flag(firmware, FU_FIRMWARE_FLAG_HAS_CHECKSUM);
	return TRUE;
}

static gboolean
fu_fit_firmware_verify_hash(FuFirmware *firmware,
			    FuFirmware *img,
			    FuFirmware *img_hash,
			    GBytes *blob,
			    GError **error)
{
	g_autofree gchar *algo = NULL;

	/* what is this */
	if (!fu_fdt_image_get_attr_str(FU_FDT_IMAGE(img_hash),
				       FU_FIT_FIRMWARE_ATTR_ALGO,
				       &algo,
				       error)) {
		g_prefix_error(error, "cannot get algo for %s: ", fu_firmware_get_id(img));
		return FALSE;
	}
	if (g_strcmp0(algo, "crc32") == 0)
		return fu_fit_firmware_verify_crc32(firmware, img, img_hash, blob, error);
	if (g_strcmp0(algo, "md5") == 0) {
		return fu_fit_firmware_verify_checksum(firmware,
						       img,
						       img_hash,
						       G_CHECKSUM_MD5,
						       blob,
						       error);
	}
	if (g_strcmp0(algo, "sha1") == 0) {
		return fu_fit_firmware_verify_checksum(firmware,
						       img,
						       img_hash,
						       G_CHECKSUM_SHA1,
						       blob,
						       error);
	}
	if (g_strcmp0(algo, "sha256") == 0) {
		return fu_fit_firmware_verify_checksum(firmware,
						       img,
						       img_hash,
						       G_CHECKSUM_SHA256,
						       blob,
						       error);
	}

	/* ignore any hashes we do not support: success */
	return TRUE;
}

static gboolean
fu_fit_firmware_verify_image(FuFirmware *firmware,
			     GInputStream *stream,
			     FuFirmware *img,
			     FwupdInstallFlags flags,
			     GError **error)
{
	g_autoptr(GBytes) blob = NULL;

	/* sanity check */
	if (!fu_fdt_image_get_attr_str(FU_FDT_IMAGE(img), "type", NULL, error))
		return FALSE;
	if (!fu_fdt_image_get_attr_str(FU_FDT_IMAGE(img), "description", NULL, error))
		return FALSE;

	/* if has data */
	blob = fu_fdt_image_get_attr(FU_FDT_IMAGE(img), FU_FIT_FIRMWARE_ATTR_DATA, NULL);
	if (blob == NULL) {
		guint32 data_size = 0x0;
		guint32 data_offset = 0x0;

		/* extra data outside of FIT image */
		if (!fu_fdt_image_get_attr_u32(FU_FDT_IMAGE(img),
					       FU_FIT_FIRMWARE_ATTR_DATA_OFFSET,
					       &data_offset,
					       error))
			return FALSE;
		if (!fu_fdt_image_get_attr_u32(FU_FDT_IMAGE(img),
					       FU_FIT_FIRMWARE_ATTR_DATA_SIZE,
					       &data_size,
					       error))
			return FALSE;
		blob = fu_input_stream_read_bytes(stream, data_offset, data_size, error);
		if (blob == NULL)
			return FALSE;
	}
	fu_dump_bytes(G_LOG_DOMAIN, "data", blob);

	/* verify any hashes we recognize */
	if ((flags & FWUPD_INSTALL_FLAG_IGNORE_CHECKSUM) == 0) {
		g_autoptr(GPtrArray) img_hashes = fu_firmware_get_images(img);
		for (guint i = 0; i < img_hashes->len; i++) {
			FuFirmware *img_hash = g_ptr_array_index(img_hashes, i);
			if (fu_firmware_get_id(img_hash) == NULL) {
				g_set_error_literal(error,
						    FWUPD_ERROR,
						    FWUPD_ERROR_INVALID_DATA,
						    "no ID for image hash");
				return FALSE;
			}
			if (g_str_has_prefix(fu_firmware_get_id(img_hash), "hash")) {
				if (!fu_fit_firmware_verify_hash(firmware,
								 img,
								 img_hash,
								 blob,
								 error))
					return FALSE;
			}
		}
	}

	/* success */
	return TRUE;
}

static gboolean
fu_fit_firmware_verify_configuration(FuFirmware *firmware,
				     FuFirmware *img,
				     FwupdInstallFlags flags,
				     GError **error)
{
	/* sanity check */
	if (!fu_fdt_image_get_attr_strlist(FU_FDT_IMAGE(img),
					   FU_FIT_FIRMWARE_ATTR_COMPATIBLE,
					   NULL,
					   error))
		return FALSE;

	/* success */
	return TRUE;
}

static gboolean
fu_fit_firmware_parse(FuFirmware *firmware,
		      GInputStream *stream,
		      gsize offset,
		      FwupdInstallFlags flags,
		      GError **error)
{
	g_autoptr(FuFirmware) img_cfgs = NULL;
	g_autoptr(FuFirmware) img_images = NULL;
	g_autoptr(FuFirmware) img_root = NULL;
	g_autoptr(GPtrArray) img_images_array = NULL;
	g_autoptr(GPtrArray) img_cfgs_array = NULL;

	/* FuFdtFirmware->parse */
	if (!FU_FIRMWARE_CLASS(fu_fit_firmware_parent_class)
		 ->parse(firmware, stream, offset, flags, error))
		return FALSE;

	/* sanity check */
	img_root = fu_firmware_get_image_by_id(firmware, NULL, error);
	if (img_root == NULL)
		return FALSE;
	if (!fu_fdt_image_get_attr_u32(FU_FDT_IMAGE(img_root),
				       FU_FIT_FIRMWARE_ATTR_TIMESTAMP,
				       NULL,
				       error))
		return FALSE;

	/* check the checksums of each image */
	img_images = fu_firmware_get_image_by_id(img_root, FU_FIT_FIRMWARE_ID_IMAGES, error);
	if (img_images == NULL)
		return FALSE;
	img_images_array = fu_firmware_get_images(img_images);
	for (guint i = 0; i < img_images_array->len; i++) {
		FuFirmware *img = g_ptr_array_index(img_images_array, i);
		if (!fu_fit_firmware_verify_image(firmware, stream, img, flags, error))
			return FALSE;
	}

	/* check the setup of each configuration */
	img_cfgs = fu_firmware_get_image_by_id(img_root, FU_FIT_FIRMWARE_ID_CONFIGURATIONS, error);
	if (img_cfgs == NULL)
		return FALSE;
	img_cfgs_array = fu_firmware_get_images(img_cfgs);
	for (guint i = 0; i < img_cfgs_array->len; i++) {
		FuFirmware *img = g_ptr_array_index(img_cfgs_array, i);
		if (!fu_fit_firmware_verify_configuration(firmware, img, flags, error))
			return FALSE;
	}

	/* success */
	return TRUE;
}

static void
fu_fit_firmware_init(FuFitFirmware *self)
{
	fu_firmware_set_images_max(FU_FIRMWARE(self), 1024);
}

static void
fu_fit_firmware_class_init(FuFitFirmwareClass *klass)
{
	FuFirmwareClass *firmware_class = FU_FIRMWARE_CLASS(klass);
	firmware_class->parse = fu_fit_firmware_parse;
}

/**
 * fu_fit_firmware_new:
 *
 * Creates a new #FuFirmware of sub type FIT
 *
 * Since: 1.8.2
 **/
FuFirmware *
fu_fit_firmware_new(void)
{
	return FU_FIRMWARE(g_object_new(FU_TYPE_FIT_FIRMWARE, NULL));
}
