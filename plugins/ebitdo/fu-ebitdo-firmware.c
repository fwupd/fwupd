/*
 * Copyright 2016 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include "fu-ebitdo-firmware.h"
#include "fu-ebitdo-struct.h"

struct _FuEbitdoFirmware {
	FuFirmware parent_instance;
};

G_DEFINE_TYPE(FuEbitdoFirmware, fu_ebitdo_firmware, FU_TYPE_FIRMWARE)

static gboolean
fu_ebitdo_firmware_parse(FuFirmware *firmware,
			 GInputStream *stream,
			 FuFirmwareParseFlags flags,
			 GError **error)
{
	guint32 version;
	gsize streamsz = 0;
	g_autoptr(FuFirmware) img_hdr = fu_firmware_new();
	g_autoptr(FuStructEbitdoHdr) st = NULL;
	g_autoptr(GInputStream) stream_hdr = NULL;
	g_autoptr(GInputStream) stream_payload = NULL;

	/* check the file size */
	st = fu_struct_ebitdo_hdr_parse_stream(stream, 0x0, error);
	if (st == NULL)
		return FALSE;
	if (!fu_input_stream_size(stream, &streamsz, error))
		return FALSE;

	/* parse version */
	version = fu_struct_ebitdo_hdr_get_version(st);
	fu_firmware_set_version_raw(firmware, version);

	/* add header */
	stream_hdr = fu_partial_input_stream_new(stream, 0x0, st->buf->len, error);
	if (stream_hdr == NULL)
		return FALSE;
	if (!fu_firmware_parse_stream(img_hdr, stream_hdr, 0x0, flags, error))
		return FALSE;
	fu_firmware_set_id(img_hdr, FU_FIRMWARE_ID_HEADER);
	if (!fu_firmware_add_image(firmware, img_hdr, error))
		return FALSE;

	/* add payload */
	stream_payload = fu_partial_input_stream_new(stream,
						     st->buf->len,
						     fu_struct_ebitdo_hdr_get_destination_len(st),
						     error);
	if (stream_payload == NULL)
		return FALSE;
	if (!fu_firmware_set_stream(firmware, stream_payload, error))
		return FALSE;
	fu_firmware_set_id(firmware, FU_FIRMWARE_ID_PAYLOAD);
	fu_firmware_set_addr(firmware, fu_struct_ebitdo_hdr_get_destination_addr(st));
	fu_firmware_set_size(firmware, st->buf->len + fu_struct_ebitdo_hdr_get_destination_len(st));
	return TRUE;
}

static GByteArray *
fu_ebitdo_firmware_write(FuFirmware *firmware, GError **error)
{
	g_autoptr(FuStructEbitdoHdr) st = fu_struct_ebitdo_hdr_new();
	g_autoptr(GBytes) blob = NULL;

	/* header then payload */
	blob = fu_firmware_get_bytes_with_patches(firmware, error);
	if (blob == NULL)
		return NULL;
	fu_struct_ebitdo_hdr_set_version(st, fu_firmware_get_version_raw(firmware));
	fu_struct_ebitdo_hdr_set_destination_addr(st, fu_firmware_get_addr(firmware));
	fu_struct_ebitdo_hdr_set_destination_len(st, g_bytes_get_size(blob));
	fu_byte_array_append_bytes(st->buf, blob);
	return g_steal_pointer(&st->buf);
}

static gchar *
fu_ebitdo_firmware_convert_version(FuFirmware *firmware, guint64 version_raw)
{
	return g_strdup_printf("%.2f", version_raw / 100.f);
}

static void
fu_ebitdo_firmware_init(FuEbitdoFirmware *self)
{
	fu_firmware_set_version_format(FU_FIRMWARE(self), FWUPD_VERSION_FORMAT_PAIR);
	fu_firmware_add_image_gtype(FU_FIRMWARE(self), FU_TYPE_FIRMWARE);
	fu_firmware_add_flag(FU_FIRMWARE(self), FU_FIRMWARE_FLAG_HAS_STORED_SIZE);
	fu_firmware_add_flag(FU_FIRMWARE(self), FU_FIRMWARE_FLAG_ALLOW_LINEAR);
}

static void
fu_ebitdo_firmware_class_init(FuEbitdoFirmwareClass *klass)
{
	FuFirmwareClass *firmware_class = FU_FIRMWARE_CLASS(klass);
	firmware_class->convert_version = fu_ebitdo_firmware_convert_version;
	firmware_class->parse = fu_ebitdo_firmware_parse;
	firmware_class->write = fu_ebitdo_firmware_write;
}
