/*
 * Copyright (C) 2016-2019 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include "fu-ebitdo-firmware.h"

struct _FuEbitdoFirmware {
	FuFirmwareClass		 parent_instance;
};

G_DEFINE_TYPE (FuEbitdoFirmware, fu_ebitdo_firmware, FU_TYPE_FIRMWARE)

/* little endian */
typedef struct __attribute__((packed)) {
	guint32		version;
	guint32		destination_addr;
	guint32		destination_len;
	guint32		reserved[4];
} FuEbitdoFirmwareHeader;

static gboolean
fu_ebitdo_firmware_parse (FuFirmware *firmware,
			  GBytes *fw,
			  guint64 addr_start,
			  guint64 addr_end,
			  FwupdInstallFlags flags,
			  GError **error)
{
	FuEbitdoFirmwareHeader *hdr;
	guint32 payload_len;
	g_autofree gchar *version = NULL;
	g_autoptr(FuFirmwareImage) img_hdr = fu_firmware_image_new (NULL);
	g_autoptr(FuFirmwareImage) img_payload = fu_firmware_image_new (NULL);
	g_autoptr(GBytes) fw_hdr = NULL;
	g_autoptr(GBytes) fw_payload = NULL;

	/* corrupt */
	if (g_bytes_get_size (fw) < sizeof (FuEbitdoFirmwareHeader)) {
		g_set_error_literal (error,
				     G_IO_ERROR,
				     G_IO_ERROR_INVALID_DATA,
				     "firmware too small for header");
		return FALSE;
	}

	/* check the file size */
	hdr = (FuEbitdoFirmwareHeader *) g_bytes_get_data (fw, NULL);
	payload_len = (guint32) (g_bytes_get_size (fw) - sizeof (FuEbitdoFirmwareHeader));
	if (payload_len != GUINT32_FROM_LE(hdr->destination_len)) {
		g_set_error (error,
			     G_IO_ERROR,
			     G_IO_ERROR_INVALID_DATA,
			     "file size incorrect, expected 0x%04x got 0x%04x",
			     (guint) GUINT32_FROM_LE (hdr->destination_len),
			     (guint) payload_len);
		return FALSE;
	}

	/* check if this is firmware */
	for (guint i = 0; i < 4; i++) {
		if (hdr->reserved[i] != 0x0) {
			g_set_error (error,
				     G_IO_ERROR,
				     G_IO_ERROR_INVALID_DATA,
				     "data invalid, reserved[%u] = 0x%04x",
				     i, hdr->reserved[i]);
			return FALSE;
		}
	}

	/* parse version */
	version = g_strdup_printf ("%.2f", GUINT32_FROM_LE(hdr->version) / 100.f);
	fu_firmware_set_version (firmware, version);

	/* add header */
	fw_hdr = g_bytes_new_from_bytes (fw, 0x0, sizeof(FuEbitdoFirmwareHeader));
	fu_firmware_image_set_id (img_hdr, FU_FIRMWARE_IMAGE_ID_HEADER);
	fu_firmware_image_set_bytes (img_hdr, fw_hdr);
	fu_firmware_add_image (firmware, img_hdr);

	/* add payload */
	fw_payload = g_bytes_new_from_bytes (fw, sizeof(FuEbitdoFirmwareHeader), payload_len);
	fu_firmware_image_set_id (img_payload, FU_FIRMWARE_IMAGE_ID_PAYLOAD);
	fu_firmware_image_set_addr (img_payload, GUINT32_FROM_LE(hdr->destination_addr));
	fu_firmware_image_set_bytes (img_payload, fw_payload);
	fu_firmware_add_image (firmware, img_payload);
	return TRUE;
}

static void
fu_ebitdo_firmware_init (FuEbitdoFirmware *self)
{
}

static void
fu_ebitdo_firmware_class_init (FuEbitdoFirmwareClass *klass)
{
	FuFirmwareClass *klass_firmware = FU_FIRMWARE_CLASS (klass);
	klass_firmware->parse = fu_ebitdo_firmware_parse;
}

FuFirmware *
fu_ebitdo_firmware_new (void)
{
	return FU_FIRMWARE (g_object_new (FU_TYPE_EBITDO_FIRMWARE, NULL));
}
