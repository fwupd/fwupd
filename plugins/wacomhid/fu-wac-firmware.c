/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2018 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include <string.h>

#include "dfu-element.h"
#include "dfu-format-srec.h"
#include "dfu-image.h"

#include "fu-wac-firmware.h"

#include "fwupd-error.h"

typedef struct {
	guint32 addr;
	guint32 sz;
	guint32 prog_start_addr;
} DfuFirmwareWacHeaderRecord;

/**
 * fu_wac_firmware_parse_data:
 * @firmware: a #DfuFirmware
 * @bytes: data to parse
 * @flags: some #DfuFirmwareParseFlags
 * @error: a #GError, or %NULL
 *
 * Unpacks into a firmware object from DfuSe data.
 *
 * Returns: %TRUE for success
 **/
gboolean
fu_wac_firmware_parse_data (DfuFirmware *firmware,
			 GBytes *bytes,
			 DfuFirmwareParseFlags flags,
			 GError **error)
{
	gsize len;
	guint8 *data;
	g_auto(GStrv) lines = NULL;
	g_autoptr(GString) image_buffer = NULL;
	g_autofree gchar *data_str = NULL;
	guint8 images_cnt = 0;
	g_autoptr(GPtrArray) header_infos = g_ptr_array_new_with_free_func (g_free);

	/* check the prefix (BE) */
	data = (guint8 *) g_bytes_get_data (bytes, &len);
	if (memcmp (data, "WACOM", 5) != 0) {
		g_set_error_literal (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_INTERNAL,
				     "invalid .wac prefix");
		return FALSE;
	}

	/* parse each line */
	data_str = g_strndup ((const gchar *) data, len);
	lines = g_strsplit (data_str, "\n", -1);
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
				header_image_cnt = dfu_utils_buffer_parse_uint4 (lines[i] + 5);
				for (guint j = 0; j < header_image_cnt; j++) {
					DfuFirmwareWacHeaderRecord *hdr = g_new0 (DfuFirmwareWacHeaderRecord, 1);
					hdr->addr = dfu_utils_buffer_parse_uint32 (lines[i] + (j * 16) + 6);
					hdr->sz = dfu_utils_buffer_parse_uint32 (lines[i] + (j * 16) + 14);
					g_ptr_array_add (header_infos, hdr);
					g_debug ("header_fw%u_addr: 0x%x", j, hdr->addr);
					g_debug ("header_fw%u_sz:   0x%x", j, hdr->sz);
				}
				continue;
			}

			/* firmware headline record */
			if (cmdlen == 13) {
				DfuFirmwareWacHeaderRecord *hdr;
				guint8 idx = dfu_utils_buffer_parse_uint4 (lines[i] + 2);
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
				hdr->prog_start_addr = dfu_utils_buffer_parse_uint32 (lines[i] + 3);
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
			g_autoptr(DfuImage) image = dfu_image_new ();
			DfuFirmwareWacHeaderRecord *hdr;

			/* get the correct relocated start address */
			if (images_cnt >= header_infos->len) {
				g_set_error (error,
					     FWUPD_ERROR,
					     FWUPD_ERROR_INTERNAL,
					     "%s without header", cmd);
				return FALSE;
			}
			hdr = g_ptr_array_index (header_infos, images_cnt);

			/* parse SREC file and add as image */
			blob = g_bytes_new (image_buffer->str, image_buffer->len);
			if (!dfu_image_from_srec (image, blob, hdr->addr, flags, error))
				return FALSE;

			/* the alt-setting is used for the firmware index */
			dfu_image_set_alt_setting (image, images_cnt);
			dfu_firmware_add_image (firmware, image);
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
	dfu_firmware_set_format (firmware, DFU_FIRMWARE_FORMAT_SREC);
	return TRUE;
}
