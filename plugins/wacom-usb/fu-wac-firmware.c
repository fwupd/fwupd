/*
 * Copyright (C) 2018 Richard Hughes <richard@hughsie.com>
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
	if (len < 5 || memcmp (data, "WACOM", 5) != 0) {
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
			gsize cmdlen = strlen (lines[i]);

			/* header info record */
			if (cmdlen > 3 && memcmp (lines[i] + 2, "COM", 3) == 0) {
				guint8 header_image_cnt = 0;
				if (cmdlen != 40) {
					g_set_error (error,
						     FWUPD_ERROR,
						     FWUPD_ERROR_INTERNAL,
						     "invalid header, got %" G_GSIZE_FORMAT " bytes",
						     cmdlen);
					return FALSE;
				}
				if (!fu_firmware_strparse_uint4_safe (lines[i],
								      cmdlen,
								      5,
								      &header_image_cnt,
								      error))
					return FALSE;
				for (guint j = 0; j < header_image_cnt; j++) {
					g_autofree FuFirmwareWacHeaderRecord *hdr = NULL;
					hdr = g_new0 (FuFirmwareWacHeaderRecord, 1);
					if (!fu_firmware_strparse_uint32_safe (lines[i], cmdlen,
									       (j * 16) + 6,
									       &hdr->addr,
									       error))
						return FALSE;
					if (!fu_firmware_strparse_uint32_safe (lines[i], cmdlen,
									       (j * 16) + 14,
									       &hdr->sz,
									       error))
						return FALSE;
					g_debug ("header_fw%u_addr: 0x%x", j, hdr->addr);
					g_debug ("header_fw%u_sz:   0x%x", j, hdr->sz);
					g_ptr_array_add (header_infos, g_steal_pointer (&hdr));
				}
				continue;
			}

			/* firmware headline record */
			if (cmdlen == 13) {
				FuFirmwareWacHeaderRecord *hdr;
				guint8 idx = 0;
				if (!fu_firmware_strparse_uint4_safe (lines[i],
								      cmdlen,
								      2,
								      &idx,
								      error))
					return FALSE;
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
				if (!fu_firmware_strparse_uint32_safe (lines[i],
								       cmdlen,
								       3,
								       &hdr->prog_start_addr,
								       error))
					return FALSE;
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
			g_autoptr(GBytes) fw_srec = NULL;
			g_autoptr(FuFirmware) firmware_srec = fu_srec_firmware_new ();
			g_autoptr(FuFirmware) img = fu_firmware_new ();
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
			fw_srec = fu_firmware_get_bytes (firmware_srec, error);
			if (fw_srec == NULL)
				return FALSE;
			fu_firmware_set_bytes (img, fw_srec);
			fu_firmware_set_addr (img, fu_firmware_get_addr (firmware_srec));
			fu_firmware_set_idx (img, images_cnt);
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

static guint8
fu_wac_firmware_calc_checksum (GByteArray *buf)
{
	guint8 csum = 0;
	for (guint i = 0; i < buf->len; i++)
		csum += buf->data[i];
	return csum ^ 0xFF;
}

static GBytes *
fu_wac_firmware_write (FuFirmware *firmware, GError **error)
{
	g_autoptr(GPtrArray) images = fu_firmware_get_images (firmware);
	g_autoptr(GString) str = g_string_new (NULL);
	g_autoptr(GByteArray) buf_hdr = g_byte_array_new ();

	/* fw header */
	for (guint i = 0; i < images->len; i++) {
		FuFirmware *img = g_ptr_array_index (images, i);
		fu_byte_array_append_uint32 (buf_hdr, fu_firmware_get_addr (img), G_BIG_ENDIAN);
		fu_byte_array_append_uint32 (buf_hdr, fu_firmware_get_size (img), G_BIG_ENDIAN);
	}
	g_string_append_printf (str, "WACOM%u", images->len);
	for (guint i = 0; i < buf_hdr->len; i++)
		g_string_append_printf (str, "%02X", buf_hdr->data[i]);
	g_string_append_printf (str, "%02X\n", fu_wac_firmware_calc_checksum (buf_hdr));

	/* payload */
	for (guint i = 0; i < images->len; i++) {
		FuFirmware *img = g_ptr_array_index (images, i);
		g_autoptr(GBytes) img_blob = NULL;
		g_autoptr(GByteArray) buf_img = g_byte_array_new ();

		/* img header */
		g_string_append_printf (str, "WA%u", (guint) fu_firmware_get_idx (img) + 1);
		fu_byte_array_append_uint32 (buf_img, fu_firmware_get_addr (img), G_BIG_ENDIAN);
		for (guint j = 0; j < buf_img->len; j++)
			g_string_append_printf (str, "%02X", buf_img->data[j]);
		g_string_append_printf (str, "%02X\n", fu_wac_firmware_calc_checksum (buf_img));

		/* srec */
		img_blob = fu_firmware_write (img, error);
		if (img_blob == NULL)
			return NULL;
		g_string_append_len (str,
				     (const gchar *) g_bytes_get_data (img_blob, NULL),
				     g_bytes_get_size (img_blob));
	}

	/* success */
	return g_string_free_to_bytes (g_steal_pointer (&str));
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
	klass_firmware->write = fu_wac_firmware_write;
}

FuFirmware *
fu_wac_firmware_new (void)
{
	return FU_FIRMWARE (g_object_new (FU_TYPE_WAC_FIRMWARE, NULL));
}
