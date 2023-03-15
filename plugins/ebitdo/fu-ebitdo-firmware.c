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
	FuStruct *st = fu_struct_lookup(firmware, "EbitdoFirmwareHdr");
	guint32 payload_len;
	gsize bufsz = 0;
	guint32 version;
	const guint8 *buf = g_bytes_get_data(fw, &bufsz);
	g_autofree gchar *version_str = NULL;
	g_autoptr(FuFirmware) img_hdr = fu_firmware_new();
	g_autoptr(GBytes) fw_hdr = NULL;
	g_autoptr(GBytes) fw_payload = NULL;

	/* parse */
	if (!fu_struct_unpack_full(st, buf, bufsz, offset, FU_STRUCT_FLAG_NONE, error))
		return FALSE;

	/* check the file size */
	payload_len = (guint32)(g_bytes_get_size(fw) - fu_struct_size(st));
	if (payload_len != fu_struct_get_u32(st, "destination_len")) {
		g_set_error(error,
			    G_IO_ERROR,
			    G_IO_ERROR_INVALID_DATA,
			    "file size incorrect, expected 0x%04x got 0x%04x",
			    (guint)fu_struct_get_u32(st, "destination_len"),
			    (guint)payload_len);
		return FALSE;
	}

	/* parse version */
	version = fu_struct_get_u32(st, "version");
	version_str = g_strdup_printf("%.2f", version / 100.f);
	fu_firmware_set_version(firmware, version_str);
	fu_firmware_set_version_raw(firmware, version);

	/* add header */
	fw_hdr = fu_bytes_new_offset(fw, 0x0, fu_struct_size(st), error);
	if (fw_hdr == NULL)
		return FALSE;
	fu_firmware_set_id(img_hdr, FU_FIRMWARE_ID_HEADER);
	fu_firmware_set_bytes(img_hdr, fw_hdr);
	fu_firmware_add_image(firmware, img_hdr);

	/* add payload */
	fw_payload = fu_bytes_new_offset(fw, fu_struct_size(st), payload_len, error);
	if (fw_payload == NULL)
		return FALSE;
	fu_firmware_set_id(firmware, FU_FIRMWARE_ID_PAYLOAD);
	fu_firmware_set_addr(firmware, fu_struct_get_u32(st, "destination_addr"));
	fu_firmware_set_bytes(firmware, fw_payload);
	return TRUE;
}

static GBytes *
fu_ebitdo_firmware_write(FuFirmware *firmware, GError **error)
{
	FuStruct *st = fu_struct_lookup(firmware, "EbitdoFirmwareHdr");
	g_autoptr(GByteArray) buf = NULL;
	g_autoptr(GBytes) blob = NULL;

	/* header then payload */
	blob = fu_firmware_get_bytes_with_patches(firmware, error);
	if (blob == NULL)
		return NULL;
	fu_struct_set_u32(st, "version", fu_firmware_get_version_raw(firmware));
	fu_struct_set_u32(st, "destination_addr", fu_firmware_get_addr(firmware));
	fu_struct_set_u32(st, "destination_len", g_bytes_get_size(blob));
	buf = fu_struct_pack(st);
	fu_byte_array_append_bytes(buf, blob);
	return g_byte_array_free_to_bytes(g_steal_pointer(&buf));
}

static void
fu_ebitdo_firmware_init(FuEbitdoFirmware *self)
{
	fu_struct_register(self,
			   "EbitdoFirmwareHdr {"
			   "    version: u32le,"
			   "    destination_addr: u32le,"
			   "    destination_len: u32le,"
			   "    reserved: 4u32le,"
			   "}");
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
