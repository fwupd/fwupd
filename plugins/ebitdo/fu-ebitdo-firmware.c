/*
 * Copyright 2016 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include "fu-ebitdo-firmware.h"
#include "fu-ebitdo-struct.h"

struct _FuEbitdoFirmware {
	FuFirmwareClass parent_instance;
};

G_DEFINE_TYPE(FuEbitdoFirmware, fu_ebitdo_firmware, FU_TYPE_FIRMWARE)

static gboolean
fu_ebitdo_firmware_parse(FuFirmware *firmware,
			 GInputStream *stream,
			 gsize offset,
			 FwupdInstallFlags flags,
			 GError **error)
{
	guint32 payload_len;
	guint32 version;
	gsize streamsz = 0;
	g_autoptr(FuFirmware) img_hdr = fu_firmware_new();
	g_autoptr(GByteArray) st = NULL;
	g_autoptr(GInputStream) stream_hdr = NULL;
	g_autoptr(GInputStream) stream_payload = NULL;

	/* check the file size */
	st = fu_struct_ebitdo_hdr_parse_stream(stream, offset, error);
	if (st == NULL)
		return FALSE;
	if (!fu_input_stream_size(stream, &streamsz, error))
		return FALSE;
	payload_len = (guint32)(streamsz - st->len);
	if (payload_len != fu_struct_ebitdo_hdr_get_destination_len(st)) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INVALID_DATA,
			    "file size incorrect, expected 0x%04x got 0x%04x",
			    (guint)fu_struct_ebitdo_hdr_get_destination_len(st),
			    (guint)payload_len);
		return FALSE;
	}

	/* parse version */
	version = fu_struct_ebitdo_hdr_get_version(st);
	fu_firmware_set_version_raw(firmware, version);

	/* add header */
	stream_hdr = fu_partial_input_stream_new(stream, 0x0, st->len, error);
	if (stream_hdr == NULL)
		return FALSE;
	if (!fu_firmware_parse_stream(img_hdr, stream_hdr, 0x0, flags, error))
		return FALSE;
	fu_firmware_set_id(img_hdr, FU_FIRMWARE_ID_HEADER);
	fu_firmware_add_image(firmware, img_hdr);

	/* add payload */
	stream_payload = fu_partial_input_stream_new(stream, st->len, payload_len, error);
	if (stream_payload == NULL)
		return FALSE;
	if (!fu_firmware_set_stream(firmware, stream_payload, error))
		return FALSE;
	fu_firmware_set_id(firmware, FU_FIRMWARE_ID_PAYLOAD);
	fu_firmware_set_addr(firmware, fu_struct_ebitdo_hdr_get_destination_addr(st));
	return TRUE;
}

static GByteArray *
fu_ebitdo_firmware_write(FuFirmware *firmware, GError **error)
{
	g_autoptr(GByteArray) st = fu_struct_ebitdo_hdr_new();
	g_autoptr(GBytes) blob = NULL;

	/* header then payload */
	blob = fu_firmware_get_bytes_with_patches(firmware, error);
	if (blob == NULL)
		return NULL;
	fu_struct_ebitdo_hdr_set_version(st, fu_firmware_get_version_raw(firmware));
	fu_struct_ebitdo_hdr_set_destination_addr(st, fu_firmware_get_addr(firmware));
	fu_struct_ebitdo_hdr_set_destination_len(st, g_bytes_get_size(blob));
	fu_byte_array_append_bytes(st, blob);
	return g_steal_pointer(&st);
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
}

static void
fu_ebitdo_firmware_class_init(FuEbitdoFirmwareClass *klass)
{
	FuFirmwareClass *firmware_class = FU_FIRMWARE_CLASS(klass);
	firmware_class->convert_version = fu_ebitdo_firmware_convert_version;
	firmware_class->parse = fu_ebitdo_firmware_parse;
	firmware_class->write = fu_ebitdo_firmware_write;
}
