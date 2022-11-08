/*
 * Copyright (C) 2021 Texas Instruments Incorporated
 * Copyright (C) 2022 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+ OR MIT
 */

#include "config.h"

#include "fu-ti-tps6598x-firmware.h"

struct _FuTiTps6598xFirmware {
	FuFirmware parent_instance;
};

G_DEFINE_TYPE(FuTiTps6598xFirmware, fu_ti_tps6598x_firmware, FU_TYPE_FIRMWARE)

#define FU_TI_TPS6598X_FIRMWARE_BINARY_ID   0xACEF0001
#define FU_TI_TPS6598X_FIRMWARE_PUBKEY_SIZE 0x180 /* bytes */

static gboolean
fu_ti_tps6598x_firmware_check_magic(FuFirmware *firmware, GBytes *fw, gsize offset, GError **error)
{
	guint32 magic = 0;

	if (!fu_memread_uint32_safe(g_bytes_get_data(fw, NULL),
				    g_bytes_get_size(fw),
				    offset,
				    &magic,
				    G_LITTLE_ENDIAN,
				    error)) {
		g_prefix_error(error, "failed to read magic: ");
		return FALSE;
	}
	if (magic != FU_TI_TPS6598X_FIRMWARE_BINARY_ID) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INVALID_FILE,
			    "invalid magic, expected 0x%04X got 0x%04X",
			    (guint32)FU_TI_TPS6598X_FIRMWARE_BINARY_ID,
			    magic);
		return FALSE;
	}

	/* success */
	return TRUE;
}

static gboolean
fu_ti_tps6598x_firmware_parse(FuFirmware *firmware,
			      GBytes *fw,
			      gsize offset,
			      FwupdInstallFlags flags,
			      GError **error)
{
	guint8 verbuf[3] = {0x0};
	g_autofree gchar *version_str = NULL;
	g_autoptr(FuFirmware) img_payload = fu_firmware_new();
	g_autoptr(FuFirmware) img_pubkey = fu_firmware_new();
	g_autoptr(FuFirmware) img_sig = fu_firmware_new();
	g_autoptr(GBytes) blob_payload = NULL;
	g_autoptr(GBytes) blob_pubkey = NULL;
	g_autoptr(GBytes) blob_sig = NULL;

	/* skip magic */
	offset += 0x4;

	/* pubkey */
	blob_pubkey = fu_bytes_new_offset(fw, offset, FU_TI_TPS6598X_FIRMWARE_PUBKEY_SIZE, error);
	if (blob_pubkey == NULL)
		return FALSE;
	fu_firmware_set_bytes(img_pubkey, blob_pubkey);
	fu_firmware_set_id(img_pubkey, "pubkey");
	fu_firmware_add_image(firmware, img_pubkey);
	offset += g_bytes_get_size(blob_pubkey);

	/* RSA signature */
	blob_sig = fu_bytes_new_offset(fw, offset, FU_TI_TPS6598X_FIRMWARE_PUBKEY_SIZE, error);
	if (blob_sig == NULL)
		return FALSE;
	fu_firmware_set_bytes(img_sig, blob_sig);
	fu_firmware_set_id(img_sig, FU_FIRMWARE_ID_SIGNATURE);
	fu_firmware_add_image(firmware, img_sig);
	offset += g_bytes_get_size(blob_sig);

	/* payload */
	blob_payload = fu_bytes_new_offset(fw, offset, g_bytes_get_size(fw) - offset, error);
	if (blob_payload == NULL)
		return FALSE;
	if (!fu_memcpy_safe(verbuf,
			    sizeof(verbuf),
			    0x0,
			    g_bytes_get_data(blob_payload, NULL),
			    g_bytes_get_size(blob_payload),
			    0x34,
			    sizeof(verbuf),
			    error))
		return FALSE;
	version_str = g_strdup_printf("%X.%X.%X", verbuf[2], verbuf[1], verbuf[0]);
	fu_firmware_set_version(img_payload, version_str);
	fu_firmware_set_bytes(img_payload, blob_payload);
	fu_firmware_set_id(img_payload, FU_FIRMWARE_ID_PAYLOAD);
	fu_firmware_add_image(firmware, img_payload);

	/* success */
	return TRUE;
}

static GBytes *
fu_ti_tps6598x_firmware_write(FuFirmware *firmware, GError **error)
{
	g_autoptr(GByteArray) buf = g_byte_array_new();
	g_autoptr(GBytes) blob_payload = NULL;
	g_autoptr(GBytes) blob_pubkey = NULL;
	g_autoptr(GBytes) blob_sig = NULL;

	/* magic */
	fu_byte_array_append_uint32(buf, FU_TI_TPS6598X_FIRMWARE_BINARY_ID, G_LITTLE_ENDIAN);

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
	return g_byte_array_free_to_bytes(g_steal_pointer(&buf));
}

static void
fu_ti_tps6598x_firmware_init(FuTiTps6598xFirmware *self)
{
	fu_firmware_add_flag(FU_FIRMWARE(self), FU_FIRMWARE_FLAG_DEDUPE_ID);
}

static void
fu_ti_tps6598x_firmware_class_init(FuTiTps6598xFirmwareClass *klass)
{
	FuFirmwareClass *klass_firmware = FU_FIRMWARE_CLASS(klass);
	klass_firmware->check_magic = fu_ti_tps6598x_firmware_check_magic;
	klass_firmware->parse = fu_ti_tps6598x_firmware_parse;
	klass_firmware->write = fu_ti_tps6598x_firmware_write;
}

FuFirmware *
fu_ti_tps6598x_firmware_new(void)
{
	return FU_FIRMWARE(g_object_new(FU_TYPE_TI_TPS6598X_FIRMWARE, NULL));
}
