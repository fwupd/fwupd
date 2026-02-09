/*
 * Copyright 2026 Himax Company, Ltd.
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include "fu-himax-tp-firmware.h"
#include "fu-himax-tp-hid-device.h"
#include "fu-himax-tp-struct.h"

#define FU_HIMAX_TP_FIRMWARE_HEADER_V1 0x87
#define FU_HIMAX_TP_FIRMWARE_HEADER_V2 0x56

#define FU_HIMAX_TP_MAP_CODE_TABLE_SIZE 1024 /* bytes */

struct _FuHimaxTpFirmware {
	FuFirmware parent_instance;
	guint16 vid;
	guint16 pid;
	guint16 cid;
	guint8 tp_cfg_ver;
	guint8 dd_cfg_ver;
	gchar *ic_id;
	gchar *ic_id_mod;
};

G_DEFINE_TYPE(FuHimaxTpFirmware, fu_himax_tp_firmware, FU_TYPE_FIRMWARE)

guint16
fu_himax_tp_firmware_get_cid(FuHimaxTpFirmware *self)
{
	g_return_val_if_fail(FU_IS_HIMAX_TP_FIRMWARE(self), G_MAXUINT16);
	return self->cid;
}

guint16
fu_himax_tp_firmware_get_vid(FuHimaxTpFirmware *self)
{
	g_return_val_if_fail(FU_IS_HIMAX_TP_FIRMWARE(self), G_MAXUINT16);
	return self->vid;
}

guint16
fu_himax_tp_firmware_get_pid(FuHimaxTpFirmware *self)
{
	g_return_val_if_fail(FU_IS_HIMAX_TP_FIRMWARE(self), G_MAXUINT16);
	return self->pid;
}

/*
 * Differences from standard CRC32C:
 *
 * - Standard: processes byte-by-byte with mask 0xFFFFFFFF
 * - Himax: processes 4 bytes at a time (DWORD) with mask 0x7FFFFFFF
 * - Standard poly: 0x1EDC6F41 (normal), Himax poly: 0x82F63B78 (reversed LE)
 * - Himax implementation uses right-shift with modified mask (0x7FFFFFFF)
 */
static guint32
fu_himax_tp_firmware_calculate_crc32c(guint32 crc, const guint8 *buf, gsize bufsz)
{
	const guint32 mask = 0x7FFFFFFF;
	gsize length;
	guint32 current_data;
	guint32 poly = 0x82F63B78;
	guint32 tmp;

	length = bufsz / 4;
	for (gsize i = 0; i < length; i++) {
		current_data = buf[i * 4];

		for (gsize j = 1; j < 4; j++) {
			tmp = buf[i * 4 + j];
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
fu_himax_tp_firmware_checksum_cb(const guint8 *buf, gsize bufsz, gpointer user_data, GError **error)
{
	guint32 *crc = (guint32 *)user_data;

	/* firmware size should be multiple of 4 bytes */
	if (bufsz % 4 != 0) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INVALID_DATA,
			    "buffer invalid size: 0x%x",
			    (guint)bufsz);
		return FALSE;
	}
	*crc = fu_himax_tp_firmware_calculate_crc32c(*crc, buf, bufsz);
	return TRUE;
}

static gboolean
fu_himax_tp_firmware_parse_map_code(FuHimaxTpFirmware *self,
				    GInputStream *stream,
				    gsize offset,
				    gboolean *done,
				    GError **error)
{
	guint8 cs_header_ver;
	guint32 offset_data;
	g_autoptr(FuStructHimaxTpMapCode) st = NULL;

	/* parse */
	st = fu_struct_himax_tp_map_code_parse_stream(stream, offset, error);
	if (st == NULL)
		return FALSE;

	/* there is no more data */
	if (fu_struct_himax_tp_map_code_get_cs(st) == 0x0) {
		*done = TRUE;
		return TRUE;
	}

	/* verify header */
	cs_header_ver = fu_struct_himax_tp_map_code_get_cs(st) >> 16;
	if (cs_header_ver != FU_HIMAX_TP_FIRMWARE_HEADER_V1 &&
	    cs_header_ver != FU_HIMAX_TP_FIRMWARE_HEADER_V2) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INVALID_DATA,
				    "firmware header invalid");
		return FALSE;
	}
	if (fu_sum8(st->buf->data, st->buf->len) != 0) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INVALID_DATA,
				    "firmware mapcode checksum invalid");
		return FALSE;
	}

	offset_data = fu_struct_himax_tp_map_code_get_flash_addr(st);
	if (fu_struct_himax_tp_map_code_get_mcode(st) == FU_HIMAX_TP_MAPCODE_FW_CID) {
		return fu_input_stream_read_u16(stream,
						offset_data,
						&self->cid,
						G_BIG_ENDIAN,
						error);
	}
	if (fu_struct_himax_tp_map_code_get_mcode(st) == FU_HIMAX_TP_MAPCODE_FW_VER) {
		guint16 fw_ver = 0;
		if (!fu_input_stream_read_u16(stream, offset_data, &fw_ver, G_BIG_ENDIAN, error))
			return FALSE;
		fu_firmware_set_version_raw(FU_FIRMWARE(self), fw_ver);
		return TRUE;
	}
	if (fu_struct_himax_tp_map_code_get_mcode(st) == FU_HIMAX_TP_MAPCODE_CFG_VER) {
		if (!fu_input_stream_read_u8(stream, offset_data, &self->tp_cfg_ver, error))
			return FALSE;
		if (!fu_input_stream_read_u8(stream, offset_data + 1, &self->dd_cfg_ver, error))
			return FALSE;
		return TRUE;
	}
	if (fu_struct_himax_tp_map_code_get_mcode(st) == FU_HIMAX_TP_MAPCODE_IC_ID) {
		g_autoptr(FuStructHimaxTpIcId) st_main = NULL;
		st_main = fu_struct_himax_tp_ic_id_parse_stream(stream, offset_data, error);
		if (st_main == NULL)
			return FALSE;
		self->ic_id = fu_struct_himax_tp_ic_id_get_desc(st_main);
		self->vid = fu_struct_himax_tp_ic_id_get_vid(st_main);
		self->pid = fu_struct_himax_tp_ic_id_get_pid(st_main);
		return TRUE;
	}
	if (fu_struct_himax_tp_map_code_get_mcode(st) == FU_HIMAX_TP_MAPCODE_IC_ID_MOD) {
		g_autoptr(FuStructHimaxTpIcIdMod) st_mod = NULL;
		st_mod = fu_struct_himax_tp_ic_id_mod_parse_stream(stream, offset_data, error);
		if (st_mod == NULL)
			return FALSE;
		self->ic_id_mod = fu_struct_himax_tp_ic_id_mod_get_desc(st_mod);
		return TRUE;
	}

	/* success */
	return TRUE;
}

static gboolean
fu_himax_tp_firmware_parse(FuFirmware *firmware,
			   GInputStream *stream,
			   FuFirmwareParseFlags flags,
			   GError **error)
{
	FuHimaxTpFirmware *self = FU_HIMAX_TP_FIRMWARE(firmware);
	guint32 crc = 0xFFFFFFFF;

	/* verify checksum */
	if (!fu_input_stream_chunkify(stream, fu_himax_tp_firmware_checksum_cb, &crc, error))
		return FALSE;
	if (crc != 0) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INVALID_DATA,
				    "firmware crc32c checksum invalid");
		return FALSE;
	}

	/* parse each MapCode */
	for (gsize i = 0; i < FU_HIMAX_TP_MAP_CODE_TABLE_SIZE;
	     i += FU_STRUCT_HIMAX_TP_MAP_CODE_SIZE) {
		gboolean done = FALSE;
		if (!fu_himax_tp_firmware_parse_map_code(self, stream, i, &done, error))
			return FALSE;
		if (done)
			break;
	}

	/* success */
	return TRUE;
}

static void
fu_himax_tp_firmware_export(FuFirmware *firmware, FuFirmwareExportFlags flags, XbBuilderNode *bn)
{
	FuHimaxTpFirmware *self = FU_HIMAX_TP_FIRMWARE(firmware);

	fu_xmlb_builder_insert_kv(bn, "ic_id", self->ic_id);
	fu_xmlb_builder_insert_kx(bn, "cid", self->cid);
	fu_xmlb_builder_insert_kx(bn, "tp_cfg_ver", self->tp_cfg_ver);
	fu_xmlb_builder_insert_kx(bn, "dd_cfg_ver", self->dd_cfg_ver);
}

static gboolean
fu_himax_tp_firmware_build(FuFirmware *firmware, XbNode *n, GError **error)
{
	FuHimaxTpFirmware *self = FU_HIMAX_TP_FIRMWARE(firmware);
	guint64 tmp;
	const gchar *str;

	tmp = xb_node_query_text_as_uint(n, "cid", NULL);
	if (tmp != G_MAXUINT64 && tmp <= G_MAXUINT16)
		self->cid = tmp;
	str = xb_node_query_text(n, "ic_id", NULL);
	if (str != NULL) {
		g_free(self->ic_id);
		self->ic_id = g_strdup(str);
	}
	str = xb_node_query_text(n, "ic_id_mod", NULL);
	if (str != NULL) {
		g_free(self->ic_id_mod);
		self->ic_id_mod = g_strdup(str);
	}
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
fu_himax_tp_firmware_finalize(GObject *object)
{
	FuHimaxTpFirmware *self = FU_HIMAX_TP_FIRMWARE(object);

	g_free(self->ic_id);
	g_free(self->ic_id_mod);

	G_OBJECT_CLASS(fu_himax_tp_firmware_parent_class)->finalize(object);
}

static void
fu_himax_tp_firmware_init(FuHimaxTpFirmware *self)
{
	fu_firmware_add_flag(FU_FIRMWARE(self), FU_FIRMWARE_FLAG_HAS_CHECKSUM);
}

static void
fu_himax_tp_firmware_class_init(FuHimaxTpFirmwareClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS(klass);
	FuFirmwareClass *firmware_class = FU_FIRMWARE_CLASS(klass);
	object_class->finalize = fu_himax_tp_firmware_finalize;
	firmware_class->parse = fu_himax_tp_firmware_parse;
	firmware_class->build = fu_himax_tp_firmware_build;
	firmware_class->export = fu_himax_tp_firmware_export;
}
