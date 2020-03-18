/*
 * Copyright (C) 2020 Cypress Semiconductor Corporation.
 * Copyright (C) 2020 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include "fu-common.h"

#include "fu-ccgx-cyacd-firmware.h"
#include "fu-ccgx-cyacd-firmware-image.h"

struct _FuCcgxCyacdFirmware {
	FuFirmwareClass		 parent_instance;
};

G_DEFINE_TYPE (FuCcgxCyacdFirmware, fu_ccgx_cyacd_firmware, FU_TYPE_FIRMWARE)

static gboolean
fu_ccgx_cyacd_firmware_parse (FuFirmware *firmware,
			      GBytes *fw,
			      guint64 addr_start,
			      guint64 addr_end,
			      FwupdInstallFlags flags,
			      GError **error)
{
	gsize sz = 0;
	guint idx = 0;
	const gchar *data = g_bytes_get_data (fw, &sz);
	g_auto(GStrv) lines = fu_common_strnsplit (data, sz, "\n", -1);
	g_autoptr(FuFirmwareImage) img_current = NULL;
	g_autoptr(GPtrArray) images = g_ptr_array_new_with_free_func ((GDestroyNotify) g_object_unref);

	for (guint ln = 0; lines[ln] != NULL; ln++) {
		g_strdelimit (lines[ln], "\r\x1a", '\0');
		if (lines[ln][0] == '\0')
			continue;

		/* create a new image section */
		if (lines[ln][0] != ':') {
			if (img_current != NULL)
				g_object_unref (img_current);
			img_current = fu_ccgx_cyacd_firmware_image_new ();
			if (!fu_ccgx_cyacd_firmware_image_parse_header (FU_CCGX_CYACD_FIRMWARE_IMAGE (img_current),
									lines[ln], error))
				return FALSE;
			g_ptr_array_add (images, g_object_ref (img_current));
			fu_firmware_image_set_idx (img_current, idx++);

		/* data */
		} else {
			if (img_current == NULL) {
				g_set_error_literal (error,
						     FWUPD_ERROR,
						     FWUPD_ERROR_NOT_SUPPORTED,
						     "no header record before data");
				return FALSE;
			}
			if (!fu_ccgx_cyacd_firmware_image_add_record (FU_CCGX_CYACD_FIRMWARE_IMAGE (img_current),
								      lines[ln] + 1, error)) {
				g_prefix_error (error, "error on line %u: ", ln + 1);
				return FALSE;
			}
		}
	}
	if (images->len == 0) {
		g_set_error_literal (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_NOT_SUPPORTED,
				     "no images found in file");
		return FALSE;
	}
	for (guint i = 0; i < images->len; i++) {
		FuFirmwareImage *img = g_ptr_array_index (images, i);
		fu_firmware_add_image (firmware, img);
	}

	/* success */
	return TRUE;
}

static void
fu_ccgx_cyacd_firmware_init (FuCcgxCyacdFirmware *self)
{
}

static void
fu_ccgx_cyacd_firmware_class_init (FuCcgxCyacdFirmwareClass *klass)
{
	FuFirmwareClass *klass_firmware = FU_FIRMWARE_CLASS (klass);
	klass_firmware->parse = fu_ccgx_cyacd_firmware_parse;
}

FuFirmware *
fu_ccgx_cyacd_firmware_new (void)
{
	return FU_FIRMWARE (g_object_new (FU_TYPE_CCGX_CYACD_FIRMWARE, NULL));
}
