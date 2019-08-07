/*
 * Copyright (C) 2018-2019 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include <string.h>

#include "fu-common.h"
#include "fu-srec-firmware.h"
#include "fu-firmware-common.h"
#include "fu-wac-firmware.h"

#include "fwupd-error.h"

struct _FuWacFirmware {
	FuFirmware		 parent_instance;
};

G_DEFINE_TYPE (FuWacFirmware, fu_wac_firmware, FU_TYPE_FIRMWARE)

typedef struct {
	guint32 addr;
	guint32 sz;
	guint32 prog_start_addr;
} FuFirmwareWacHeaderRecord;

static gboolean
fu_wac_firmware_parse (FuFirmware *firmware,
		       GBytes *fw,
		       guint64 addr_start,
		       guint64 addr_end,
		       FwupdInstallFlags flags,
		       GError **error)
{
	gsize len;
	guint8 *data;
	guint8 images_cnt = 0;
	g_auto(GStrv) lines = NULL;
	g_autoptr(GPtrArray) header_infos = g_ptr_array_new_with_free_func (g_free);
	g_autoptr(GString) image_buffer = NULL;

	/* check the prefix (BE) */
	data = (guint8 *) g_bytes_get_data (fw, &len);
	if (memcmp (data, "WACOM", 5) != 0) {
		g_set_error_literal (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_INTERNAL,
				     "invalid .wac prefix");
		return FALSE;
	}

	/* parse each line */
	lines = fu_common_strnsplit ((const gchar *) data, len, "\n", -1);
	for (guint i = 0; lines[i] != NULL; i++) {
		g_autofree gchar *cmd = g_strndup (lines[i], 2);

		/* remove windows line endings */
		g_strdelimit (lines[i], "\r", '\0');

		/* Wacom-specific metadata */
		if (g_strcmp0 (cmd, "WA") == 0) {
			guint cmdlen = strlen (lines[i]);

			/* header info record */
			if (memcmp (lines[i] + 2, "COM", 3) == 0) {
				guint8 header_image_cnt = 0;
				if (cmdlen != 40) {
					g_set_error (error,
						     FWUPD_ERROR,
						     FWUPD_ERROR_INTERNAL,
						     "invalid header, got %u bytes",
						     cmdlen);
					return FALSE;
				}
				header_image_cnt = fu_firmware_strparse_uint4 (lines[i] + 5);
				for (guint j = 0; j < header_image_cnt; j++) {
					FuFirmwareWacHeaderRecord *hdr = g_new0 (FuFirmwareWacHeaderRecord, 1);
					hdr->addr = fu_firmware_strparse_uint32 (lines[i] + (j * 16) + 6);
					hdr->sz = fu_firmware_strparse_uint32 (lines[i] + (j * 16) + 14);
					g_ptr_array_add (header_infos, hdr);
					g_debug ("header_fw%u_addr: 0x%x", j, hdr->addr);
					g_debug ("header_fw%u_sz:   0x%x", j, hdr->sz);
				}
				continue;
			}

			/* firmware headline record */
			if (cmdlen == 13) {
				FuFirmwareWacHeaderRecord *hdr;
				guint8 idx = fu_firmware_strparse_uint4 (lines[i] + 2);
				if (idx == 0) {
					g_set_error (error,
						     FWUPD_ERROR,
						     FWUPD_ERROR_INTERNAL,
						     "headline %u invalid",
						     idx);
					return FALSE;
				}
				if (idx > header_infos->len) {
					g_set_error (error,
						     FWUPD_ERROR,
						     FWUPD_ERROR_INTERNAL,
						     "headline %u exceeds header count %u",
						     idx, header_infos->len);
					return FALSE;
				}
				hdr = g_ptr_array_index (header_infos, idx - 1);
				hdr->prog_start_addr = fu_firmware_strparse_uint32 (lines[i] + 3);
				if (hdr->prog_start_addr != hdr->addr) {
					g_set_error (error,
						     FWUPD_ERROR,
						     FWUPD_ERROR_INTERNAL,
						     "programming address 0x%x != "
						     "base address 0x%0x for idx %u",
						     hdr->prog_start_addr,
						     hdr->addr,
						     idx);
					return FALSE;
				}
				g_debug ("programing-start-address: 0x%x", hdr->prog_start_addr);
				continue;
			}

			g_debug ("unknown Wacom-specific metadata");
			continue;
		}

		/* start */
		if (g_strcmp0 (cmd, "S0") == 0) {
			if (image_buffer != NULL) {
				g_set_error_literal (error,
						     FWUPD_ERROR,
						     FWUPD_ERROR_INTERNAL,
						     "duplicate S0 without S7");
				return FALSE;
			}
			image_buffer = g_string_new (NULL);
		}

		/* these are things we want to include in the image */
		if (g_strcmp0 (cmd, "S0") == 0 ||
		    g_strcmp0 (cmd, "S1") == 0 ||
		    g_strcmp0 (cmd, "S2") == 0 ||
		    g_strcmp0 (cmd, "S3") == 0 ||
		    g_strcmp0 (cmd, "S5") == 0 ||
		    g_strcmp0 (cmd, "S7") == 0 ||
		    g_strcmp0 (cmd, "S8") == 0 ||
		    g_strcmp0 (cmd, "S9") == 0) {
			if (image_buffer == NULL) {
				g_set_error (error,
					     FWUPD_ERROR,
					     FWUPD_ERROR_INTERNAL,
					     "%s without S0", cmd);
				return FALSE;
			}
			g_string_append_printf (image_buffer, "%s\n", lines[i]);
		}

		/* end */
		if (g_strcmp0 (cmd, "S7") == 0) {
			g_autoptr(GBytes) blob = NULL;
			g_autoptr(FuFirmware) firmware_srec = fu_srec_firmware_new ();
			g_autoptr(FuFirmwareImage) img = NULL;
			FuFirmwareWacHeaderRecord *hdr;

			/* get the correct relocated start address */
			if (images_cnt >= header_infos->len) {
				g_set_error (error,
					     FWUPD_ERROR,
					     FWUPD_ERROR_INTERNAL,
					     "%s without header", cmd);
				return FALSE;
			}
			hdr = g_ptr_array_index (header_infos, images_cnt);

			if (image_buffer == NULL) {
				g_set_error (error,
					     FWUPD_ERROR,
					     FWUPD_ERROR_INTERNAL,
					     "%s with missing image buffer", cmd);
				return FALSE;
			}

			/* parse SREC file and add as image */
			blob = g_bytes_new (image_buffer->str, image_buffer->len);
			if (!fu_firmware_parse_full (firmware_srec, blob, hdr->addr, 0x0, flags, error))
				return FALSE;
			img = fu_firmware_get_image_default (firmware_srec, error);
			if (img == NULL)
				return FALSE;
			fu_firmware_image_set_idx (img, images_cnt);
			fu_firmware_add_image (firmware, img);
			images_cnt++;

			/* clear the image buffer */
			g_string_free (image_buffer, TRUE);
			image_buffer = NULL;
		}
	}

	/* verify data is complete */
	if (image_buffer != NULL) {
		g_set_error_literal (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_INTERNAL,
				     "truncated data: no S7");
		return FALSE;
	}

	/* ensure this matched the header */
	if (header_infos->len != images_cnt) {
		g_set_error (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_INTERNAL,
			     "not enough images %u for header count %u",
			     images_cnt,
			     header_infos->len);
		return FALSE;
	}

	/* success */
	return TRUE;
}

static void
fu_wac_firmware_init (FuWacFirmware *self)
{
}

static void
fu_wac_firmware_class_init (FuWacFirmwareClass *klass)
{
	FuFirmwareClass *klass_firmware = FU_FIRMWARE_CLASS (klass);
	klass_firmware->parse = fu_wac_firmware_parse;
}

FuFirmware *
fu_wac_firmware_new (void)
{
	return FU_FIRMWARE (g_object_new (FU_TYPE_WAC_FIRMWARE, NULL));
}
