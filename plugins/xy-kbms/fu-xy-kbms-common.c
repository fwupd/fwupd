/*
 * Copyright 2026 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include "fu-xy-kbms-common.h"
#include "fu-xy-kbms-struct.h"

/* the vendor tool only checksums the first 128 bytes */
#define FU_XY_KBMS_CONFIG_CRC_SIZE 0x80

guint32
fu_xy_kbms_config_crc(const guint8 *buf, gsize bufsz)
{
	return fu_crc32(FU_CRC_KIND_B32_STANDARD, buf, MIN(bufsz, FU_XY_KBMS_CONFIG_CRC_SIZE));
}

/* the names used by the vendor tool */
const gchar *
fu_xy_kbms_board_type_to_name(FuXyKbmsBoardType board_type)
{
	if (board_type == FU_XY_KBMS_BOARD_TYPE_DEBUG)
		return "Debug board";
	if (board_type == FU_XY_KBMS_BOARD_TYPE_TP_WIRELESS)
		return "ThinkPad wireless conversion board";
	if (board_type == FU_XY_KBMS_BOARD_TYPE_MECH_WIRELESS)
		return "Mechanical keyboard wireless conversion board";
	if (board_type == FU_XY_KBMS_BOARD_TYPE_TP_MECH)
		return "TrackPoint mechanical keyboard board";
	if (board_type == FU_XY_KBMS_BOARD_TYPE_TP60_MECH)
		return "TrackPoint 60% mechanical keyboard board";
	if (board_type == FU_XY_KBMS_BOARD_TYPE_TP_WIRELESS2)
		return "ThinkPad wireless conversion board 2";
	if (board_type == FU_XY_KBMS_BOARD_TYPE_TP60_MECH2)
		return "TrackPoint 60% mechanical keyboard board 2";
	if (board_type == FU_XY_KBMS_BOARD_TYPE_TP_WIRELESS3)
		return "ThinkPad wireless conversion board 3";
	if (board_type == FU_XY_KBMS_BOARD_TYPE_WIRED_USB)
		return "Wired USB board";
	return NULL;
}

gboolean
fu_xy_kbms_config_update_crc(GByteArray *buf, GError **error)
{
	return fu_memwrite_uint32_safe(buf->data,
				       buf->len,
				       FU_STRUCT_XY_KBMS_CONFIG_OFFSET_CRC,
				       fu_xy_kbms_config_crc(buf->data, buf->len),
				       G_LITTLE_ENDIAN,
				       error);
}

gboolean
fu_xy_kbms_config_validate(GByteArray *buf, GError **error)
{
	guint32 crc;
	g_autoptr(FuStructXyKbmsConfig) st = NULL;

	if (buf->len != FU_STRUCT_XY_KBMS_CONFIG_SIZE) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INVALID_DATA,
			    "config has wrong length: 0x%x, expected 0x%x",
			    buf->len,
			    (guint)FU_STRUCT_XY_KBMS_CONFIG_SIZE);
		return FALSE;
	}
	st = fu_struct_xy_kbms_config_parse(buf->data, buf->len, 0x0, error);
	if (st == NULL)
		return FALSE;
	crc = fu_xy_kbms_config_crc(buf->data, buf->len);
	if (crc != fu_struct_xy_kbms_config_get_crc(st)) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INVALID_DATA,
			    "config checksum invalid: 0x%08x, expected 0x%08x",
			    fu_struct_xy_kbms_config_get_crc(st),
			    crc);
		return FALSE;
	}

	/* success */
	return TRUE;
}
