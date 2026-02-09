/*
 * Copyright 2026 Himax Company, Ltd.
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include "fu-himaxtp-firmware.h"
#include "fu-himaxtp-hid-device.h"
#include "fu-himaxtp-struct.h"

#define CRC32C_POLY_LE (0x82F63B78)
#define HX_HEADER_V1   (0x87)
#define HX_HEADER_V2   (0x56)

struct _FuHimaxtpFirmware {
	FuFirmware parent_instance;
	guint16 vid;
	guint16 pid;
	guint16 cid;
	guint8 tp_cfg_ver;
	guint8 dd_cfg_ver;
	guint16 fw_ver;
	gchar ic_id[12];
	gchar ic_id_mod[2];
};

G_DEFINE_TYPE(FuHimaxtpFirmware, fu_himaxtp_firmware, FU_TYPE_FIRMWARE)

union FuHxData {
	guint32 dword;
	/* nocheck:zero-init */
	guint16 word[2];
	guint8 byte[4];
};

#pragma pack(1)

struct FuHimaxtpMapCode {
	union FuHxData mcode;
	union FuHxData flash_addr;
	union FuHxData size;
	union FuHxData cs;
};

#pragma pack()

guint16
fu_himaxtp_firmware_get_cid(FuHimaxtpFirmware *self)
{
	return self->cid;
}

guint16
fu_himaxtp_firmware_get_vid(FuHimaxtpFirmware *self)
{
	return self->vid;
}

guint16
fu_himaxtp_firmware_get_pid(FuHimaxtpFirmware *self)
{
	return self->pid;
}

guint8
fu_himaxtp_firmware_get_tp_cfg_ver(FuHimaxtpFirmware *self)
{
	return self->tp_cfg_ver;
}

guint8
fu_himaxtp_firmware_get_dd_cfg_ver(FuHimaxtpFirmware *self)
{
	return self->dd_cfg_ver;
}

guint16
fu_himaxtp_firmware_get_fw_ver(FuHimaxtpFirmware *self)
{
	return self->fw_ver;
}

gchar *
fu_himaxtp_firmware_get_ic_id(FuHimaxtpFirmware *self)
{
	return self->ic_id;
}

gchar *
fu_himaxtp_firmware_get_ic_id_mod(FuHimaxtpFirmware *self)
{
	return self->ic_id_mod;
}

static guint8
fu_himaxtp_firmware_sum8(const guint8 *data, gsize len)
{
	guint8 sum = 0;

	for (gsize i = 0; i < len; i++)
		sum += data[i];
	return sum;
}

static gboolean
fu_himaxtp_firmware_all_zero(const guint8 *data, gsize len)
{
	for (gsize i = 0; i < len; i++) {
		if (data[i] != 0)
			return FALSE;
	}
	return TRUE;
}

static guint32
fu_himaxtp_firmware_calculate_crc32c(const guint8 *data, gsize len)
{
	/* Himax HW CRC use special ALG */
	guint32 crc = 0xFFFFFFFF;
	guint32 poly = CRC32C_POLY_LE;
	const guint32 mask = 0x7FFFFFFF;
	gsize length;
	guint32 current_data;
	guint32 tmp;

	length = len / 4;
	for (gsize i = 0; i < length; i++) {
		current_data = data[i * 4];

		for (gsize j = 1; j < 4; j++) {
			tmp = data[i * 4 + j];
			current_data += (tmp << (j * 8));
		}
		crc ^= current_data;
		for (gsize j = 0; j < 32; j++) {
			if (crc & 1) {
				crc = ((crc >> 1) & mask) ^ poly;
			} else {
				crc = (crc >> 1) & mask;
			}
		}
	}

	return crc;
}

static gboolean
fu_himaxtp_firmware_validate(FuFirmware *firmware,
			     GInputStream *stream,
			     gsize offset,
			     GError **error)
{
	gsize streamsz = 0;
	struct FuHimaxtpMapCode mapcode = {0};
	g_autoptr(GByteArray) st = NULL;

	if (!fu_input_stream_size(stream, &streamsz, error))
		return FALSE;

	if (streamsz < 255 * 1024) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INVALID_FILE,
				    "firmware image was too small");
		return FALSE;
	}

	st = fu_input_stream_read_byte_array(stream, offset, streamsz, NULL, error);
	if (st == NULL) {
		g_prefix_error_literal(error, "failed to read firmware: ");
		return FALSE;
	}

	if (st->len != streamsz) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_READ,
			    "requested %u bytes but got %u bytes",
			    (guint)streamsz,
			    (guint)st->len);
		return FALSE;
	}

	if (fu_himaxtp_firmware_calculate_crc32c(st->data, st->len) != 0) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INVALID_DATA,
				    "firmware crc32c checksum invalid");
		return FALSE;
	}

	if (!fu_memcpy_safe((guint8 *)&mapcode,
			    sizeof(struct FuHimaxtpMapCode),
			    0,
			    st->data,
			    st->len,
			    0,
			    sizeof(struct FuHimaxtpMapCode),
			    error))
		return FALSE;

	if ((mapcode.cs.byte[2] != HX_HEADER_V1 && mapcode.cs.byte[2] != HX_HEADER_V2) ||
	    fu_himaxtp_firmware_sum8(st->data, sizeof(struct FuHimaxtpMapCode)) != 0) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INVALID_DATA,
				    "firmware mapcode checksum invalid");
		return FALSE;
	}

	/* success */
	return TRUE;
}

static gboolean
fu_himaxtp_firmware_parse(FuFirmware *firmware,
			  GInputStream *stream,
			  FuFirmwareParseFlags flags,
			  GError **error)
{
	FuHimaxtpFirmware *self = FU_HIMAXTP_FIRMWARE(firmware);
	guint32 offset;
	gsize streamsz = 0;
	struct FuHimaxtpMapCode mapcode = {0};
	g_autoptr(GByteArray) st = NULL;

	if (!fu_input_stream_size(stream, &streamsz, error))
		return FALSE;

	st = fu_input_stream_read_byte_array(stream, 0, streamsz, NULL, error);
	if (st == NULL) {
		g_prefix_error_literal(error, "failed to read firmware: ");
		return FALSE;
	}

	if (st->len != streamsz) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_READ,
			    "requested 0x%x and got 0x%x",
			    (guint)streamsz,
			    (guint)st->len);
		return FALSE;
	}

	for (gsize i = 0; i < 1024; i += sizeof(struct FuHimaxtpMapCode)) {
		if (!fu_memcpy_safe((guint8 *)&mapcode,
				    sizeof(struct FuHimaxtpMapCode),
				    0,
				    st->data + i,
				    sizeof(struct FuHimaxtpMapCode),
				    0,
				    sizeof(struct FuHimaxtpMapCode),
				    error))
			return FALSE;

		if (fu_himaxtp_firmware_sum8((const guint8 *)st->data + i,
					     sizeof(struct FuHimaxtpMapCode)) != 0 ||
		    fu_himaxtp_firmware_all_zero((const guint8 *)&mapcode,
						 sizeof(struct FuHimaxtpMapCode)))
			break;

		if (!fu_memcpy_safe((guint8 *)&mapcode,
				    sizeof(struct FuHimaxtpMapCode),
				    0,
				    st->data + i,
				    sizeof(struct FuHimaxtpMapCode),
				    0,
				    sizeof(struct FuHimaxtpMapCode),
				    error))
			return FALSE;

		offset = mapcode.flash_addr.dword;
		switch (mapcode.mcode.dword) {
		case FU_HIMAXTP_MAPCODE_FW_CID:
			self->cid = (guint16)st->data[offset] << 8 | (guint16)st->data[offset + 1];
			break;
		case FU_HIMAXTP_MAPCODE_FW_VER:
			self->fw_ver =
			    (guint16)st->data[offset] << 8 | (guint16)st->data[offset + 1];
			break;
		case FU_HIMAXTP_MAPCODE_CFG_VER:
			self->tp_cfg_ver = (guint8)st->data[offset];
			self->dd_cfg_ver = (guint8)st->data[offset + 1];
			break;
		case FU_HIMAXTP_MAPCODE_IC_ID:
			if (!fu_memcpy_safe((guint8 *)self->ic_id,
					    sizeof(self->ic_id),
					    0,
					    st->data + offset,
					    sizeof(self->ic_id),
					    0,
					    sizeof(self->ic_id) - 1,
					    error))
				return FALSE;

			offset += sizeof(self->ic_id);
			self->vid = (guint16)st->data[offset] << 8 | (guint16)st->data[offset + 1];
			self->pid =
			    (guint16)st->data[offset + 2] << 8 | (guint16)st->data[offset + 3];
			break;
		case FU_HIMAXTP_MAPCODE_IC_ID_MOD:
			if (!fu_memcpy_safe((guint8 *)self->ic_id_mod,
					    sizeof(self->ic_id_mod),
					    0,
					    st->data + offset,
					    sizeof(self->ic_id_mod),
					    0,
					    sizeof(self->ic_id_mod),
					    error))
				return FALSE;

			self->ic_id_mod[sizeof(self->ic_id_mod) - 1] = '\0';
			break;
		default:
			continue;
		}
	}

	/* success */
	return TRUE;
}

static void
fu_himaxtp_firmware_export(FuFirmware *firmware, FuFirmwareExportFlags flags, XbBuilderNode *bn)
{
	FuHimaxtpFirmware *self = FU_HIMAXTP_FIRMWARE(firmware);

	fu_xmlb_builder_insert_kv(bn, "ic_id", self->ic_id);
	fu_xmlb_builder_insert_kx(bn, "cid", self->cid);
	fu_xmlb_builder_insert_kx(bn, "fw_ver", self->fw_ver);
	fu_xmlb_builder_insert_kx(bn, "tp_cfg_ver", self->tp_cfg_ver);
	fu_xmlb_builder_insert_kx(bn, "dd_cfg_ver", self->dd_cfg_ver);
}

static gboolean
fu_himaxtp_firmware_build(FuFirmware *firmware, XbNode *n, GError **error)
{
	FuHimaxtpFirmware *self = FU_HIMAXTP_FIRMWARE(firmware);
	guint64 tmp;
	const gchar *str;

	tmp = xb_node_query_text_as_uint(n, "cid", NULL);
	if (tmp != G_MAXUINT64 && tmp <= G_MAXUINT16)
		self->cid = tmp;

	tmp = xb_node_query_text_as_uint(n, "fw_ver", NULL);
	if (tmp != G_MAXUINT64 && tmp <= G_MAXUINT16)
		self->fw_ver = tmp;

	str = xb_node_query_text(n, "ic_id", NULL);
	if (str != NULL)
		g_strlcpy(self->ic_id, str, sizeof(self->ic_id));

	str = xb_node_query_text(n, "ic_id_mod", NULL);
	if (str != NULL)
		g_strlcpy(self->ic_id_mod, str, sizeof(self->ic_id_mod));

	tmp = xb_node_query_text_as_uint(n, "tp_cfg_ver", NULL);
	if (tmp != G_MAXUINT64 && tmp <= G_MAXUINT8)
		self->tp_cfg_ver = tmp;

	tmp = xb_node_query_text_as_uint(n, "dd_cfg_ver", NULL);
	if (tmp != G_MAXUINT64 && tmp <= G_MAXUINT8)
		self->dd_cfg_ver = tmp;

	/* success */
	return TRUE;
}

static void
fu_himaxtp_firmware_init(FuHimaxtpFirmware *self)
{
}

static void
fu_himaxtp_firmware_class_init(FuHimaxtpFirmwareClass *klass)
{
	FuFirmwareClass *firmware_class = FU_FIRMWARE_CLASS(klass);
	firmware_class->validate = fu_himaxtp_firmware_validate;
	firmware_class->parse = fu_himaxtp_firmware_parse;
	firmware_class->build = fu_himaxtp_firmware_build;
	firmware_class->export = fu_himaxtp_firmware_export;
}

FuFirmware *
fu_himaxtp_firmware_new(void)
{
	return FU_FIRMWARE(g_object_new(FU_TYPE_HIMAXTP_FIRMWARE, NULL));
}
