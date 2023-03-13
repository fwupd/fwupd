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

static gboolean
fu_ebitdo_firmware_parse(FuFirmware *firmware,
			 GBytes *fw,
			 gsize offset,
			 FwupdInstallFlags flags,
			 GError **error)
{
	gsize bufsz = 0;
	guint32 destination_addr = 0;
	guint32 destination_len = 0;
	guint32 payload_len;
	guint32 version_raw = 0;
	gsize offset_local = offset;
	const guint8 *buf = g_bytes_get_data(fw, &bufsz);
	g_autofree gchar *version = NULL;
	g_autoptr(FuFirmware) img_hdr = fu_firmware_new();
	g_autoptr(GBytes) fw_hdr = NULL;
	g_autoptr(GBytes) fw_payload = NULL;

	if (!fu_struct_unpack_from("<LLL4L",
				   error,
				   buf,
				   bufsz,
				   &offset,
				   &version_raw,
				   &destination_addr,
				   &destination_len,
				   NULL)) /* reserved */
		return FALSE;

	/* check the file size */
	payload_len = (guint32)(g_bytes_get_size(fw) - fu_struct_size("<LLL4L", NULL));
	if (payload_len != destination_len) {
		g_set_error(error,
			    G_IO_ERROR,
			    G_IO_ERROR_INVALID_DATA,
			    "file size incorrect, expected 0x%04x got 0x%04x",
			    (guint)destination_len,
			    (guint)payload_len);
		return FALSE;
	}

	/* parse version */
	version = g_strdup_printf("%.2f", version_raw / 100.f);
	fu_firmware_set_version(firmware, version);
	fu_firmware_set_version_raw(firmware, version_raw);

	/* add header */
	fw_hdr = fu_bytes_new_offset(fw, offset_local, offset - offset_local, error);
	if (fw_hdr == NULL)
		return FALSE;
	fu_firmware_set_id(img_hdr, FU_FIRMWARE_ID_HEADER);
	fu_firmware_set_bytes(img_hdr, fw_hdr);
	fu_firmware_add_image(firmware, img_hdr);

	/* add payload */
	fw_payload = fu_bytes_new_offset(fw, offset - offset_local, payload_len, error);
	if (fw_payload == NULL)
		return FALSE;
	fu_firmware_set_id(firmware, FU_FIRMWARE_ID_PAYLOAD);
	fu_firmware_set_addr(firmware, destination_addr);
	fu_firmware_set_bytes(firmware, fw_payload);
	return TRUE;
}

static GBytes *
fu_ebitdo_firmware_write(FuFirmware *firmware, GError **error)
{
	const guint32 reserved[] = {0x0, 0x0, 0x0, 0x0};
	g_autoptr(GByteArray) buf = g_byte_array_new();
	g_autoptr(GBytes) blob = NULL;

	/* header then payload */
	blob = fu_firmware_get_bytes_with_patches(firmware, error);
	if (blob == NULL)
		return NULL;
	buf = fu_struct_pack("<LLL4L",
			     error,
			     (guint)fu_firmware_get_version_raw(firmware),
			     fu_firmware_get_addr(firmware),
			     g_bytes_get_size(blob),
			     reserved);

	if (buf == NULL)
		return NULL;
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
