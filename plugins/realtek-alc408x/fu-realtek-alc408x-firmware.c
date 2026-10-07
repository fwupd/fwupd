/*
 * Copyright 2026 NVIDIA Corporation
 * Author: Vishnu Raghav <vraghav@nvidia.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include "fu-realtek-alc408x-common.h"
#include "fu-realtek-alc408x-firmware.h"
#include "fu-realtek-alc408x-struct.h"

struct _FuRealtekAlc408xFirmware {
	FuFirmware parent_instance;
	guint16 chip;
};

G_DEFINE_TYPE(FuRealtekAlc408xFirmware, fu_realtek_alc408x_firmware, FU_TYPE_FIRMWARE)

static void
fu_realtek_alc408x_firmware_export(FuFirmware *firmware,
				   FuFirmwareExportFlags flags,
				   XbBuilderNode *bn)
{
	FuRealtekAlc408xFirmware *self = FU_REALTEK_ALC408X_FIRMWARE(firmware);
	fu_xmlb_builder_insert_kx(bn, "chip", self->chip);
}

static gboolean
fu_realtek_alc408x_firmware_build(FuFirmware *firmware, XbNode *n, GError **error)
{
	FuRealtekAlc408xFirmware *self = FU_REALTEK_ALC408X_FIRMWARE(firmware);
	guint64 tmp;

	tmp = xb_node_query_text_as_uint(n, "chip", NULL);
	if (tmp != G_MAXUINT64 && tmp <= G_MAXUINT16)
		self->chip = tmp;

	/* success */
	return TRUE;
}

static gboolean
fu_realtek_alc408x_firmware_validate(FuFirmware *firmware,
				     FuInputStream *stream,
				     gsize offset,
				     GError **error)
{
	return fu_struct_realtek_alc408x_fw_hdr_validate_stream(
	    stream,
	    offset + FU_REALTEK_ALC408X_FIRMWARE_HDR_OFFSET,
	    error);
}

static gboolean
fu_realtek_alc408x_firmware_parse(FuFirmware *firmware,
				  FuInputStream *stream,
				  FuFirmwareParseFlags flags,
				  GError **error)
{
	FuRealtekAlc408xFirmware *self = FU_REALTEK_ALC408X_FIRMWARE(firmware);
	gsize streamsz = 0;
	g_autoptr(FuStructRealtekAlc408xFwHdr) st_hdr = NULL;

	/* the image is written to a whole 64 KiB flash bank */
	if (!fu_input_stream_size(stream, &streamsz, error))
		return FALSE;
	if (streamsz != FU_REALTEK_ALC408X_FIRMWARE_SIZE) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INVALID_FILE,
			    "wrong file size, expected 0x%x and got 0x%x",
			    (guint)FU_REALTEK_ALC408X_FIRMWARE_SIZE,
			    (guint)streamsz);
		return FALSE;
	}

	st_hdr =
	    fu_struct_realtek_alc408x_fw_hdr_parse_stream(stream,
							  FU_REALTEK_ALC408X_FIRMWARE_HDR_OFFSET,
							  error);
	if (st_hdr == NULL)
		return FALSE;
	self->chip = fu_struct_realtek_alc408x_fw_hdr_get_chip(st_hdr);
	fu_firmware_set_version_raw(
	    firmware,
	    ((guint64)fu_struct_realtek_alc408x_fw_hdr_get_version_hi(st_hdr) << 32) |
		fu_struct_realtek_alc408x_fw_hdr_get_version_lo(st_hdr));

	/* success */
	return TRUE;
}

static GByteArray *
fu_realtek_alc408x_firmware_write(FuFirmware *firmware, GError **error)
{
	FuRealtekAlc408xFirmware *self = FU_REALTEK_ALC408X_FIRMWARE(firmware);
	guint64 version_raw = fu_firmware_get_version_raw(firmware);
	g_autoptr(FuStructRealtekAlc408xFwHdr) st_hdr = NULL;
	g_autoptr(GByteArray) buf = g_byte_array_new();
	g_autoptr(GBytes) blob = NULL;

	/* start from the parsed image so the code around the header is kept */
	blob = fu_firmware_get_bytes(firmware, NULL);
	if (blob != NULL) {
		fu_byte_array_append_bytes(buf, blob);
		if (buf->len != FU_REALTEK_ALC408X_FIRMWARE_SIZE) {
			g_set_error(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INVALID_DATA,
				    "wrong image size, expected 0x%x and got 0x%x",
				    (guint)FU_REALTEK_ALC408X_FIRMWARE_SIZE,
				    buf->len);
			return NULL;
		}
		st_hdr =
		    fu_struct_realtek_alc408x_fw_hdr_parse(buf->data,
							   buf->len,
							   FU_REALTEK_ALC408X_FIRMWARE_HDR_OFFSET,
							   error);
		if (st_hdr == NULL)
			return NULL;
	} else {
		fu_byte_array_set_size(buf, FU_REALTEK_ALC408X_FIRMWARE_SIZE, 0xFF);
		st_hdr = fu_struct_realtek_alc408x_fw_hdr_new();
	}

	/* update the header */
	fu_struct_realtek_alc408x_fw_hdr_set_chip(st_hdr, self->chip);
	fu_struct_realtek_alc408x_fw_hdr_set_version_hi(st_hdr, version_raw >> 32);
	fu_struct_realtek_alc408x_fw_hdr_set_version_lo(st_hdr, version_raw & G_MAXUINT32);
	if (!fu_memcpy_safe(buf->data,
			    buf->len,
			    FU_REALTEK_ALC408X_FIRMWARE_HDR_OFFSET,
			    st_hdr->buf->data,
			    st_hdr->buf->len,
			    0x0,
			    st_hdr->buf->len,
			    error))
		return NULL;

	/* success */
	return g_steal_pointer(&buf);
}

static gchar *
fu_realtek_alc408x_firmware_convert_version(FuFirmware *firmware, guint64 version_raw)
{
	return fu_realtek_alc408x_version_to_string(version_raw);
}

guint16
fu_realtek_alc408x_firmware_get_chip(FuRealtekAlc408xFirmware *self)
{
	g_return_val_if_fail(FU_IS_REALTEK_ALC408X_FIRMWARE(self), G_MAXUINT16);
	return self->chip;
}

static void
fu_realtek_alc408x_firmware_init(FuRealtekAlc408xFirmware *self)
{
	self->chip = FU_REALTEK_ALC408X_CHIP;
	fu_firmware_set_version_format(FU_FIRMWARE(self), FWUPD_VERSION_FORMAT_PLAIN);
	fu_firmware_add_flag(FU_FIRMWARE(self), FU_FIRMWARE_FLAG_NO_AUTO_DETECTION);
}

static void
fu_realtek_alc408x_firmware_class_init(FuRealtekAlc408xFirmwareClass *klass)
{
	FuFirmwareClass *firmware_class = FU_FIRMWARE_CLASS(klass);
	firmware_class->convert_version = fu_realtek_alc408x_firmware_convert_version;
	firmware_class->validate = fu_realtek_alc408x_firmware_validate;
	firmware_class->parse = fu_realtek_alc408x_firmware_parse;
	firmware_class->write = fu_realtek_alc408x_firmware_write;
	firmware_class->build = fu_realtek_alc408x_firmware_build;
	firmware_class->export = fu_realtek_alc408x_firmware_export;
	fu_firmware_set_size_max(firmware_class, FU_REALTEK_ALC408X_FIRMWARE_SIZE);
}

FuFirmware *
fu_realtek_alc408x_firmware_new(void)
{
	return FU_FIRMWARE(g_object_new(FU_TYPE_REALTEK_ALC408X_FIRMWARE, NULL));
}
