/*
 * Copyright (C) 2020 Benson Leung <bleung@chromium.org>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include "fu-common.h"
#include "fu-fmap-firmware.h"

#define FMAP_SIGNATURE		"__FMAP__"
#define FMAP_AREANAME		"FMAP"

G_DEFINE_TYPE (FuFmapFirmware, fu_fmap_firmware, FU_TYPE_FIRMWARE)

/* returns size of fmap data structure if successful, <0 to indicate error */
static gint
fmap_size (FuFmap *fmap)
{
	if (fmap == NULL)
		return -1;

	return sizeof (*fmap) + (fmap->nareas * sizeof (FuFmap));
}

/* brute force linear search */
static gboolean
fmap_lsearch (const guint8 *image, gsize len, gsize *offset, GError **error)
{
	gsize i;
	gboolean fmap_found = FALSE;

	if (offset == NULL) {
		g_set_error_literal (error,
				     G_IO_ERROR,
				     G_IO_ERROR_INVALID_DATA,
				     "offset return not valid");
		return FALSE;
	}

	for (i = 0; i < len - strlen(FMAP_SIGNATURE); i++) {
		if (!memcmp(&image[i],
		            FMAP_SIGNATURE,
		            strlen(FMAP_SIGNATURE))) {
			fmap_found = TRUE;
			break;
		}
	}

	if (!fmap_found) {
		g_set_error_literal (error,
				     G_IO_ERROR,
				     G_IO_ERROR_INVALID_DATA,
				     "fmap not found using linear search");
		return FALSE;
	}

	if (i + fmap_size ((FuFmap *)&image[i]) > len) {
		g_set_error_literal (error,
				     G_IO_ERROR,
				     G_IO_ERROR_INVALID_DATA,
				     "malformed fmap too close to end of image");
		return FALSE;
	}

	*offset = i;
	return TRUE;
}

/* if image length is a power of 2, use binary search */
static gboolean
fmap_bsearch (const guint8 *image, gsize len, gsize *offset, GError **error)
{
	gsize i;
	gboolean fmap_found = FALSE;

	if (offset == NULL) {
		g_set_error_literal (error,
				     G_IO_ERROR,
				     G_IO_ERROR_INVALID_DATA,
				     "offset return not valid");
		return FALSE;
	}

	/*
	 * For efficient operation, we start with the largest stride possible
	 * and then decrease the stride on each iteration. Also, check for a
	 * remainder when modding the offset with the previous stride. This
	 * makes it so that each offset is only checked once.
	 */
	for (gint stride = len / 2; stride >= 1; stride /= 2) {
		if (fmap_found)
			break;

		for (i = 0; i < len - strlen(FMAP_SIGNATURE); i += stride) {
			if ((i % (stride * 2) == 0) && (i != 0))
				continue;
			if (!memcmp (&image[i],
				     FMAP_SIGNATURE,
				     strlen (FMAP_SIGNATURE))) {
				fmap_found = TRUE;
				break;
			}
		}
	}

	if (!fmap_found) {
		g_set_error_literal (error,
				     G_IO_ERROR,
				     G_IO_ERROR_INVALID_DATA,
				     "fmap not found using binary search");
		return FALSE;
	}

	if (i + fmap_size ((FuFmap *)&image[i]) > len) {
		g_set_error_literal (error,
				     G_IO_ERROR,
				     G_IO_ERROR_INVALID_DATA,
				     "malformed fmap too close to end of image");
		return FALSE;
	}

	*offset = i;
	return TRUE;
}

static gint
popcnt (guint u)
{
	gint count;

	/* K&R method */
	for (count = 0; u; count++)
		u &= (u - 1);

	return count;
}

static gboolean
fmap_find (const guint8 *image, gsize image_len, gsize *offset, GError **error)
{
	if (image == NULL || image_len == 0) {
		g_set_error_literal (error,
				     G_IO_ERROR,
				     G_IO_ERROR_INVALID_DATA,
				     "invalid image");
		return FALSE;
	}

	if (popcnt (image_len) == 1) {
		if (!fmap_bsearch (image, image_len, offset, error)) {
			g_prefix_error (error, "failed fmap_find using bsearch: ");
			return FALSE;
		}
	} else {
		if (!fmap_lsearch (image, image_len, offset, error)) {
			g_prefix_error (error, "failed fmap_find using lsearch: ");
			return FALSE;
		}
	}

	return TRUE;
}

static gboolean
fu_fmap_firmware_parse (FuFirmware *firmware,
			GBytes *fw,
			guint64 addr_start,
			guint64 addr_end,
			FwupdInstallFlags flags,
			GError **error)
{
	FuFmapFirmwareClass *klass_firmware = FU_FMAP_FIRMWARE_GET_CLASS (firmware);
	gsize image_len;
	guint8 *image = (guint8 *)g_bytes_get_data (fw, &image_len);
	gsize offset;
	const FuFmap *fmap;

	/* corrupt */
	if (g_bytes_get_size (fw) < sizeof (FuFmap)) {
		g_set_error_literal (error,
				     G_IO_ERROR,
				     G_IO_ERROR_INVALID_DATA,
				     "firmware too small for fmap");
		return FALSE;
	}

	if (!fmap_find (image, image_len, &offset, error)) {
		g_prefix_error (error, "cannot find fmap in image: ");
		return FALSE;
	}

	fmap = (const FuFmap *)(image + offset);

	if (fmap->size != image_len) {
		g_set_error (error,
			     G_IO_ERROR,
			     G_IO_ERROR_INVALID_DATA,
			     "file size incorrect, expected 0x%04x got 0x%04x",
			     (guint) fmap->size,
			     (guint) image_len);
		return FALSE;
	}

	if (fmap->nareas < 1) {
		g_set_error (error,
			     G_IO_ERROR,
			     G_IO_ERROR_INVALID_DATA,
			     "number of areas too small, got %" G_GUINT16_FORMAT,
			     fmap->nareas);
		return FALSE;
	}

	for (gsize i = 0; i < fmap->nareas; i++) {
		const FuFmapArea *area = &fmap->areas[i];
		g_autoptr(FuFirmwareImage) img = NULL;
		g_autoptr(GBytes) bytes = NULL;

		img = fu_firmware_image_new (NULL);
		bytes = fu_common_bytes_new_offset (fw,
						    (gsize) area->offset,
						    (gsize) area->size,
						    error);
		if (bytes == NULL)
			return FALSE;
		fu_firmware_image_set_id (img, (const gchar *) area->name);
		fu_firmware_image_set_idx (img, i + 1);
		fu_firmware_image_set_addr (img, (guint64) area->offset);
		fu_firmware_image_set_bytes (img, bytes);
		fu_firmware_add_image (firmware, img);

		if (g_strcmp0 ((const char *)area->name, FMAP_AREANAME) == 0) {
			g_autofree gchar *version = g_strdup_printf ("%d.%d",
								     fmap->ver_major,
								     fmap->ver_minor);
			fu_firmware_image_set_version (img, version);
		}
	}

	/* subclassed */
	if (klass_firmware->parse != NULL) {
		if (!klass_firmware->parse (firmware, fw, addr_start, addr_end, flags, error))
			return FALSE;
	}

	/* success */
	return TRUE;
}

static void
fu_fmap_firmware_init (FuFmapFirmware *self)
{
}

static void
fu_fmap_firmware_class_init (FuFmapFirmwareClass *klass)
{
	FuFirmwareClass *klass_firmware = FU_FIRMWARE_CLASS (klass);
	klass_firmware->parse = fu_fmap_firmware_parse;
}

/**
 * fu_fmap_firmware_new
 *
 * Creates a new #FuFirmware of sub type fmap
 *
 * Since: 1.5.0
 **/
FuFirmware *
fu_fmap_firmware_new (void)
{
	return FU_FIRMWARE (g_object_new (FU_TYPE_FMAP_FIRMWARE, NULL));
}
