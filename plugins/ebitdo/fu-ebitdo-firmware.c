/*
 * Copyright (C) 2016 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include <fwupdplugin.h>

#include "fu-ebitdo-firmware.h"

struct _FuEbitdoFirmware {
	FuFirmwareClass parent_instance;
};

G_DEFINE_TYPE(FuEbitdoFirmware, fu_ebitdo_firmware, FU_TYPE_FIRMWARE)

/* little endian */
typedef struct __attribute__((packed)) {
	guint32 version;
	guint32 destination_addr;
	guint32 destination_len;
	guint32 reserved[4];
} FuEbitdoFirmwareHeader;

static gboolean
fu_ebitdo_firmware_parse(FuFirmware *firmware,
			 GBytes *fw,
			 gsize offset,
			 FwupdInstallFlags flags,
			 GError **error)
{
	FuEbitdoFirmwareHeader *hdr;
	guint32 payload_len;
	g_autofree gchar *version = NULL;
	g_autoptr(FuFirmware) img_hdr = fu_firmware_new();
	g_autoptr(GBytes) fw_hdr = NULL;
	g_autoptr(GBytes) fw_payload = NULL;

	/* corrupt */
	if (g_bytes_get_size(fw) < sizeof(FuEbitdoFirmwareHeader)) {
		g_set_error_literal(error,
				    G_IO_ERROR,
				    G_IO_ERROR_INVALID_DATA,
				    "firmware too small for header");
		return FALSE;
	}

	/* check the file size */
	hdr = (FuEbitdoFirmwareHeader *)g_bytes_get_data(fw, NULL);
	payload_len = (guint32)(g_bytes_get_size(fw) - sizeof(FuEbitdoFirmwareHeader));
	if (payload_len != GUINT32_FROM_LE(hdr->destination_len)) {
		g_set_error(error,
			    G_IO_ERROR,
			    G_IO_ERROR_INVALID_DATA,
			    "file size incorrect, expected 0x%04x got 0x%04x",
			    (guint)GUINT32_FROM_LE(hdr->destination_len),
			    (guint)payload_len);
		return FALSE;
	}

	/* check if this is firmware */
	for (guint i = 0; i < 4; i++) {
		if (hdr->reserved[i] != 0x0) {
			g_set_error(error,
				    G_IO_ERROR,
				    G_IO_ERROR_INVALID_DATA,
				    "data invalid, reserved[%u] = 0x%04x",
				    i,
				    hdr->reserved[i]);
			return FALSE;
		}
	}

	/* parse version */
	version = g_strdup_printf("%.2f", GUINT32_FROM_LE(hdr->version) / 100.f);
	fu_firmware_set_version(firmware, version);
	fu_firmware_set_version_raw(firmware, GUINT32_FROM_LE(hdr->version));

	/* add header */
	fw_hdr = fu_bytes_new_offset(fw, 0x0, sizeof(FuEbitdoFirmwareHeader), error);
	if (fw_hdr == NULL)
		return FALSE;
	fu_firmware_set_id(img_hdr, FU_FIRMWARE_ID_HEADER);
	fu_firmware_set_bytes(img_hdr, fw_hdr);
	fu_firmware_add_image(firmware, img_hdr);

	/* add payload */
	fw_payload = fu_bytes_new_offset(fw, sizeof(FuEbitdoFirmwareHeader), payload_len, error);
	if (fw_payload == NULL)
		return FALSE;
	fu_firmware_set_id(firmware, FU_FIRMWARE_ID_PAYLOAD);
	fu_firmware_set_addr(firmware, GUINT32_FROM_LE(hdr->destination_addr));
	fu_firmware_set_bytes(firmware, fw_payload);
	return TRUE;
}

static GBytes *
fu_ebitdo_firmware_write(FuFirmware *firmware, GError **error)
{
	g_autoptr(GByteArray) buf = g_byte_array_new();
	g_autoptr(GBytes) blob = NULL;

	/* header then payload */
	blob = fu_firmware_get_bytes_with_patches(firmware, error);
	if (blob == NULL)
		return NULL;
	fu_byte_array_append_uint32(buf, fu_firmware_get_version_raw(firmware), G_LITTLE_ENDIAN);
	fu_byte_array_append_uint32(buf, fu_firmware_get_addr(firmware), G_LITTLE_ENDIAN);
	fu_byte_array_append_uint32(buf, g_bytes_get_size(blob), G_LITTLE_ENDIAN);
	for (guint i = 0; i < 4; i++)
		fu_byte_array_append_uint32(buf, 0, G_LITTLE_ENDIAN);
	fu_byte_array_append_bytes(buf, blob);
	return g_byte_array_free_to_bytes(g_steal_pointer(&buf));
}

static void
fu_ebitdo_firmware_init(FuEbitdoFirmware *self)
{
}

static void
fu_ebitdo_firmware_class_init(FuEbitdoFirmwareClass *klass)
{
	FuFirmwareClass *klass_firmware = FU_FIRMWARE_CLASS(klass);
	klass_firmware->parse = fu_ebitdo_firmware_parse;
	klass_firmware->write = fu_ebitdo_firmware_write;
}

FuFirmware *
fu_ebitdo_firmware_new(void)
{
	return FU_FIRMWARE(g_object_new(FU_TYPE_EBITDO_FIRMWARE, NULL));
}
