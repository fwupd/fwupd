/*
 * Copyright 2021 Texas Instruments Incorporated
 * Copyright 2022 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later OR MIT
 */

#include "config.h"

#include "fu-ti-tps6598x-firmware.h"
#include "fu-ti-tps6598x-struct.h"

struct _FuTiTps6598xFirmware {
	FuFirmware parent_instance;
};

G_DEFINE_TYPE(FuTiTps6598xFirmware, fu_ti_tps6598x_firmware, FU_TYPE_FIRMWARE)

#define FU_TI_TPS6598X_FIRMWARE_PUBKEY_SIZE 0x180 /* bytes */

static gboolean
fu_ti_tps6598x_firmware_validate(FuFirmware *firmware,
				 GInputStream *stream,
				 gsize offset,
				 GError **error)
{
	return fu_struct_ti_tps6598x_firmware_hdr_validate_stream(stream, offset, error);
}

static gboolean
fu_ti_tps6598x_firmware_parse(FuFirmware *firmware,
			      GInputStream *stream,
			      FwupdInstallFlags flags,
			      GError **error)
{
	guint8 verbuf[3] = {0x0};
	gsize offset = 0;
	gsize streamsz = 0;
	g_autofree gchar *version_str = NULL;
	g_autoptr(FuFirmware) img_payload = fu_firmware_new();
	g_autoptr(FuFirmware) img_pubkey = fu_firmware_new();
	g_autoptr(FuFirmware) img_sig = fu_firmware_new();
	g_autoptr(GInputStream) stream_payload = NULL;
	g_autoptr(GInputStream) stream_pubkey = NULL;
	g_autoptr(GInputStream) stream_sig = NULL;

	/* skip magic */
	offset += 0x4;
	if (!fu_input_stream_size(stream, &streamsz, error))
		return FALSE;

	/* pubkey */
	stream_pubkey =
	    fu_partial_input_stream_new(stream, offset, FU_TI_TPS6598X_FIRMWARE_PUBKEY_SIZE, error);
	if (stream_pubkey == NULL)
		return FALSE;
	if (!fu_firmware_parse_stream(img_pubkey, stream_pubkey, 0x0, flags, error))
		return FALSE;
	fu_firmware_set_id(img_pubkey, "pubkey");
	fu_firmware_add_image(firmware, img_pubkey);
	offset += FU_TI_TPS6598X_FIRMWARE_PUBKEY_SIZE;

	/* RSA signature */
	stream_sig =
	    fu_partial_input_stream_new(stream, offset, FU_TI_TPS6598X_FIRMWARE_PUBKEY_SIZE, error);
	if (stream_sig == NULL)
		return FALSE;
	if (!fu_firmware_parse_stream(img_sig, stream_sig, 0x0, flags, error))
		return FALSE;
	fu_firmware_set_id(img_sig, FU_FIRMWARE_ID_SIGNATURE);
	fu_firmware_add_image(firmware, img_sig);
	offset += FU_TI_TPS6598X_FIRMWARE_PUBKEY_SIZE;

	/* payload */
	stream_payload = fu_partial_input_stream_new(stream, offset, streamsz - offset, error);
	if (stream_payload == NULL)
		return FALSE;
	if (!fu_firmware_parse_stream(img_payload, stream_payload, 0x0, flags, error))
		return FALSE;
	if (!fu_input_stream_read_safe(stream,
				       verbuf,
				       sizeof(verbuf),
				       0x0,
				       0x34, /* offset */
				       sizeof(verbuf),
				       error))
		return FALSE;
	version_str = g_strdup_printf("%X.%X.%X", verbuf[2], verbuf[1], verbuf[0]);
	fu_firmware_set_version(img_payload, version_str);
	fu_firmware_set_id(img_payload, FU_FIRMWARE_ID_PAYLOAD);
	fu_firmware_add_image(firmware, img_payload);

	/* success */
	return TRUE;
}

static GByteArray *
fu_ti_tps6598x_firmware_write(FuFirmware *firmware, GError **error)
{
	g_autoptr(GByteArray) buf = g_byte_array_new();
	g_autoptr(GBytes) blob_payload = NULL;
	g_autoptr(GBytes) blob_pubkey = NULL;
	g_autoptr(GBytes) blob_sig = NULL;

	/* magic */
	fu_byte_array_append_uint32(buf,
				    FU_STRUCT_TI_TPS6598X_FIRMWARE_HDR_DEFAULT_MAGIC,
				    G_LITTLE_ENDIAN);

	/* pubkey */
	blob_pubkey = fu_firmware_get_image_by_id_bytes(firmware, "pubkey", error);
	if (blob_pubkey == NULL)
		return NULL;
	fu_byte_array_append_bytes(buf, blob_pubkey);

	/* sig */
	blob_sig = fu_firmware_get_image_by_id_bytes(firmware, FU_FIRMWARE_ID_SIGNATURE, error);
	if (blob_sig == NULL)
		return NULL;
	fu_byte_array_append_bytes(buf, blob_sig);

	/* payload */
	blob_payload = fu_firmware_get_image_by_id_bytes(firmware, FU_FIRMWARE_ID_PAYLOAD, error);
	if (blob_payload == NULL)
		return NULL;
	fu_byte_array_append_bytes(buf, blob_payload);

	/* add EOF */
	return g_steal_pointer(&buf);
}

static void
fu_ti_tps6598x_firmware_init(FuTiTps6598xFirmware *self)
{
	fu_firmware_add_flag(FU_FIRMWARE(self), FU_FIRMWARE_FLAG_DEDUPE_ID);
}

static void
fu_ti_tps6598x_firmware_class_init(FuTiTps6598xFirmwareClass *klass)
{
	FuFirmwareClass *firmware_class = FU_FIRMWARE_CLASS(klass);
	firmware_class->validate = fu_ti_tps6598x_firmware_validate;
	firmware_class->parse = fu_ti_tps6598x_firmware_parse;
	firmware_class->write = fu_ti_tps6598x_firmware_write;
}
