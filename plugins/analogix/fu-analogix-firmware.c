/*
 * Copyright (C) 2021 Xiaotian Cui <xtcui@analogixsemi.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include "fu-analogix-common.h"
#include "fu-analogix-firmware.h"


struct _FuAnalogixFirmware {
	FuFirmwareClass parent_instance;
};

G_DEFINE_TYPE (FuAnalogixFirmware, fu_analogix_firmware, FU_TYPE_FIRMWARE)

static gboolean
fu_analogix_firmware_parse (FuFirmware *firmware,
			    GBytes *fw,
			    guint64 addr_start,
			    guint64 addr_end,
			    FwupdInstallFlags flags,
			    GError **error)
{
	guint8 *bin_buf = g_malloc0 (MAX_FILE_SIZE);
	/* guint32 payload_len; */
	g_autoptr(FuFirmware) img_hdr = fu_firmware_new ();
	g_autoptr(FuFirmware) img_payload = fu_firmware_new ();
	guchar colon = ':';
	guchar *hex_hdr;
	gsize fw_size = 0;
	AnxImgHeader *img_header = g_malloc0 (sizeof(AnxImgHeader));
	g_autoptr(GBytes) fw_hdr = NULL;
	g_autoptr(GBytes) fw_payload = NULL;

	g_debug ("fu_analogix_firmware_parse, start:%d, end:%d, flag =%d",
		 (gint) addr_start, (gint) addr_end, (gint) flags);

	/* corrupt */
	fw_size = g_bytes_get_size (fw);
	if (bin_buf == NULL) {
		g_set_error_literal (error,
				     G_IO_ERROR,
				     G_IO_ERROR_INVALID_DATA,
				     "Malloc memory failed");
		return FALSE;
	}

	memset (bin_buf, 0xff, MAX_FILE_SIZE);
	memset (img_header, 0, sizeof(AnxImgHeader));
	g_debug ("fw_size : %ld", (long) fw_size);
	if (fw_size < HEX_LINE_HEADER_SIZE) {
		g_set_error_literal (error,
				     G_IO_ERROR,
				     G_IO_ERROR_INVALID_DATA,
				     "firmware too small for parse");
		return FALSE;
	}

	hex_hdr = (guchar *) g_bytes_get_data (fw, NULL);
	if (hex_hdr[0] != colon) {
		g_set_error_literal (error,
				     G_IO_ERROR,
				     G_IO_ERROR_INVALID_DATA,
				     "Error firmware file");
		return FALSE;
	}

	if (!parse_fw_hex_file (hex_hdr, fw_size, img_header, bin_buf))
		return FALSE;
	fw_hdr = g_bytes_new_take (img_header, sizeof(AnxImgHeader));
	fw_payload = g_bytes_new_take (bin_buf, img_header->total_len);
	if ((fw_hdr == NULL) || (fw_payload == NULL))
		return FALSE;

	fu_firmware_set_id (img_hdr, FU_FIRMWARE_ID_HEADER);
	fu_firmware_set_bytes (img_hdr, fw_hdr);
	fu_firmware_add_image (firmware, img_hdr);
	fu_firmware_set_id (img_payload, FU_FIRMWARE_ID_PAYLOAD);
	if (img_header->fw_start_addr != 0)
		fu_firmware_set_addr (img_payload, img_header->fw_start_addr);
	else if (img_header->custom_start_addr != 0)
		fu_firmware_set_addr (img_payload,
				      img_header->custom_start_addr);

	fu_firmware_set_bytes (img_payload, fw_payload);
	fu_firmware_add_image (firmware, img_payload);

	return TRUE;
}

static void
fu_analogix_firmware_init (FuAnalogixFirmware *self)
{

}

static void
fu_analogix_firmware_class_init (FuAnalogixFirmwareClass *klass)
{
	FuFirmwareClass *klass_firmware = FU_FIRMWARE_CLASS (klass);
	klass_firmware->parse = fu_analogix_firmware_parse;
}

FuFirmware *
fu_analogix_firmware_new (void)
{
	return FU_FIRMWARE (g_object_new (FU_TYPE_ANALOGIX_FIRMWARE, NULL));
}
