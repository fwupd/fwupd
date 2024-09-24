/*
 * Copyright 2023 Denis Pynkin <denis.pynkin@collabora.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include <fwupdplugin.h>

#include "fu-qc-s5gen2-firmware.h"
#include "fu-qc-s5gen2-fw-struct.h"

struct _FuQcS5gen2Firmware {
	FuFirmware parent_instance;
	guint32 file_id;
	guint8 protocol_ver;
	gchar *device_variant;
};

G_DEFINE_TYPE(FuQcS5gen2Firmware, fu_qc_s5gen2_firmware, FU_TYPE_FIRMWARE)

guint8
fu_qc_s5gen2_firmware_get_protocol_version(FuQcS5gen2Firmware *self)
{
	return self->protocol_ver;
}

/* generated ID unique for the firmware */
guint32
fu_qc_s5gen2_firmware_get_id(FuQcS5gen2Firmware *self)
{
	return self->file_id;
}

static void
fu_qc_s5gen2_firmware_export(FuFirmware *firmware, FuFirmwareExportFlags flags, XbBuilderNode *bn)
{
	FuQcS5gen2Firmware *self = FU_QC_S5GEN2_FIRMWARE(firmware);
	fu_xmlb_builder_insert_kv(bn, "device_variant", self->device_variant);
	fu_xmlb_builder_insert_kx(bn, "protocol_version", self->protocol_ver);
	fu_xmlb_builder_insert_kx(bn, "generated_file_id", self->file_id);
}

static gboolean
fu_qc_s5gen2_firmware_validate(FuFirmware *firmware,
			       GInputStream *stream,
			       gsize offset,
			       GError **error)
{
	return fu_struct_qc_fw_update_hdr_validate_stream(stream, offset, error);
}

static gboolean
fu_qc_s5gen2_firmware_parse(FuFirmware *firmware,
			    GInputStream *stream,
			    gsize offset,
			    FwupdInstallFlags flags,
			    GError **error)
{
	FuQcS5gen2Firmware *self = FU_QC_S5GEN2_FIRMWARE(firmware);
	const guint8 *device_variant;
	gsize config_offset = 26;
	guint16 config_ver;
	g_autofree gchar *ver_str = NULL;
	g_autoptr(GByteArray) hdr = NULL;

	/* FIXME: deal with encrypted? */
	hdr = fu_struct_qc_fw_update_hdr_parse_stream(stream, offset, error);
	if (hdr == NULL)
		return FALSE;

	/* protocol version */
	self->protocol_ver = fu_struct_qc_fw_update_hdr_get_protocol(hdr) - '0';
	device_variant = fu_struct_qc_fw_update_hdr_get_dev_variant(hdr, NULL);
	self->device_variant = fu_strsafe((const gchar *)device_variant, 8);

	config_offset += fu_struct_qc_fw_update_hdr_get_upgrades(hdr) * 4;
	if (!fu_input_stream_read_u16(stream, config_offset, &config_ver, G_BIG_ENDIAN, error))
		return FALSE;

	ver_str = g_strdup_printf("%u.%u.%u",
				  fu_struct_qc_fw_update_hdr_get_major(hdr),
				  fu_struct_qc_fw_update_hdr_get_minor(hdr),
				  config_ver);
	fu_firmware_set_version(firmware, ver_str);

	if (!fu_firmware_set_stream(firmware, stream, error))
		return FALSE;
	if (!fu_input_stream_compute_crc32(stream, FU_CRC_KIND_B32_STANDARD, &self->file_id, error))
		return FALSE;

	/* success */
	return TRUE;
}

static GByteArray *
fu_qc_s5gen2_firmware_write(FuFirmware *firmware, GError **error)
{
	g_autoptr(GByteArray) buf = g_byte_array_new();
	g_autoptr(GBytes) fw = NULL;

	/* data first */
	fw = fu_firmware_get_bytes_with_patches(firmware, error);
	if (fw == NULL)
		return NULL;

	fu_byte_array_append_bytes(buf, fw);

	/* success */
	return g_steal_pointer(&buf);
}

static void
fu_qc_s5gen2_firmware_init(FuQcS5gen2Firmware *self)
{
	self->device_variant = NULL;
	self->file_id = 0xFFFFFFFF;
	fu_firmware_add_flag(FU_FIRMWARE(self), FU_FIRMWARE_FLAG_HAS_STORED_SIZE);
	fu_firmware_add_flag(FU_FIRMWARE(self), FU_FIRMWARE_FLAG_HAS_CHECKSUM);
	fu_firmware_add_flag(FU_FIRMWARE(self), FU_FIRMWARE_FLAG_HAS_VID_PID);
}

static void
fu_qc_s5gen2_firmware_finalize(GObject *object)
{
	FuQcS5gen2Firmware *self = FU_QC_S5GEN2_FIRMWARE(object);
	g_free(self->device_variant);
	G_OBJECT_CLASS(fu_qc_s5gen2_firmware_parent_class)->finalize(object);
}

static void
fu_qc_s5gen2_firmware_class_init(FuQcS5gen2FirmwareClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS(klass);
	FuFirmwareClass *klass_firmware = FU_FIRMWARE_CLASS(klass);
	object_class->finalize = fu_qc_s5gen2_firmware_finalize;
	klass_firmware->validate = fu_qc_s5gen2_firmware_validate;
	klass_firmware->parse = fu_qc_s5gen2_firmware_parse;
	klass_firmware->write = fu_qc_s5gen2_firmware_write;
	klass_firmware->export = fu_qc_s5gen2_firmware_export;
}

FuFirmware *
fu_qc_s5gen2_firmware_new(void)
{
	return FU_FIRMWARE(g_object_new(FU_TYPE_QC_S5GEN2_FIRMWARE, NULL));
}
