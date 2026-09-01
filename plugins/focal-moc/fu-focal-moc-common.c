/*
 * Copyright 2026 FocalTech Systems Co., Ltd.
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include "fu-focal-moc-common.h"
#include "fu-focal-moc-struct.h"

GByteArray *
fu_focal_moc_packet_new(guint8 cmd, const guint8 *buf, gsize bufsz, GError **error)
{
	g_autoptr(FuStructFocalMocCmdReq) st_req = fu_struct_focal_moc_cmd_req_new();

	g_return_val_if_fail(buf != NULL || bufsz == 0, NULL);

	if (bufsz > G_MAXUINT16 - 1) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INVALID_DATA,
			    "payload too large: 0x%x",
			    (guint)bufsz);
		return NULL;
	}

	/* the length counts the trailing checksum as well as the payload */
	fu_struct_focal_moc_cmd_req_set_ln(st_req, (guint16)bufsz + 1);
	fu_struct_focal_moc_cmd_req_set_cmd(st_req, cmd);
	if (bufsz > 0)
		g_byte_array_append(st_req->buf, buf, bufsz);
	fu_byte_array_append_uint8(st_req->buf,
				   fu_xor8(st_req->buf->data + 1, st_req->buf->len - 1));

	/* success */
	return g_byte_array_ref(st_req->buf);
}

GByteArray *
fu_focal_moc_packet_parse(const guint8 *buf, gsize bufsz, GError **error)
{
	gsize packet_sz;
	guint16 ln;
	guint8 bcc = 0;
	guint8 bcc_actual = 0;
	g_autoptr(FuStructFocalMocCmdRsp) st = NULL;
	g_autoptr(GByteArray) data = g_byte_array_new();

	g_return_val_if_fail(buf != NULL, NULL);
	g_return_val_if_fail(error == NULL || *error == NULL, NULL);

	st = fu_struct_focal_moc_cmd_rsp_parse(buf, bufsz, 0, error);
	if (st == NULL)
		return NULL;
	ln = fu_struct_focal_moc_cmd_rsp_get_ln(st);
	if (ln < 1) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INVALID_DATA,
				    "invalid packet length 0x0");
		return NULL;
	}

	/* the device pads the response, so the declared length rather than the
	 * transfer size locates the checksum */
	packet_sz = (gsize)ln + FU_STRUCT_FOCAL_MOC_CMD_RSP_SIZE;
	if (bufsz < packet_sz) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INVALID_DATA,
			    "packet truncated: got 0x%x, expected 0x%x",
			    (guint)bufsz,
			    (guint)packet_sz);
		return NULL;
	}
	if (!fu_xor8_safe(buf, bufsz, 0x1, (gsize)ln + 2, &bcc, error))
		return NULL;
	if (!fu_memread_uint8_safe(buf, bufsz, packet_sz - 1, &bcc_actual, error))
		return NULL;
	if (bcc != bcc_actual) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INVALID_DATA,
			    "checksum mismatch: got 0x%02x, expected 0x%02x",
			    bcc_actual,
			    bcc);
		return NULL;
	}

	if (ln > 1)
		g_byte_array_append(data, buf + FU_STRUCT_FOCAL_MOC_CMD_RSP_SIZE, ln - 1);

	/* success */
	return g_steal_pointer(&data);
}

gchar *
fu_focal_moc_version_parse(const guint8 *buf, gsize bufsz, gboolean *is_bootloader, GError **error)
{
	const gchar *suffix;
	gboolean has_app;
	gboolean has_iap;
	g_autofree gchar *version = NULL;

	g_return_val_if_fail(is_bootloader != NULL, NULL);
	g_return_val_if_fail(error == NULL || *error == NULL, NULL);

	if (buf == NULL || bufsz == 0) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INVALID_DATA,
				    "version payload is empty");
		return NULL;
	}
	version = g_strndup((const gchar *)buf, bufsz);
	if (version[0] == '\0' || !g_utf8_validate(version, -1, NULL)) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INVALID_DATA,
				    "version is not valid text");
		return NULL;
	}

	suffix = strrchr(version, '_');
	if (suffix == NULL || suffix[1] == '\0') {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INVALID_DATA,
			    "version has no build number: %s",
			    version);
		return NULL;
	}

	/* the runtime and the bootloader share a USB product ID, so only the
	 * build string tells them apart */
	has_iap = g_strstr_len(version, -1, "_IAP_") != NULL;
	has_app = g_strstr_len(version, -1, "_APP_") != NULL;
	if (has_iap == has_app) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INVALID_DATA,
			    "cannot determine mode from version: %s",
			    version);
		return NULL;
	}
	*is_bootloader = has_iap;

	/* success */
	return g_strdup(suffix + 1);
}
