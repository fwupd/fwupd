/*
 * Copyright 2026 FocalTech Systems Co., Ltd.
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include "fu-focal-moc-common.h"

#define FU_FOCAL_MOC_FP_VERSION_MAX_SIZE 64

GByteArray *
fu_focal_moc_packet_new(guint8 command, const guint8 *data, gsize data_sz, GError **error)
{
	g_autoptr(FuStructFocalMocPacketHeader) st = NULL;

	if (data_sz > 0 && data == NULL) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INVALID_DATA,
				    "payload is missing");
		return NULL;
	}
	if (data_sz > G_MAXUINT16 - 1) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INVALID_DATA,
			    "payload too large: 0x%x",
			    (guint)data_sz);
		return NULL;
	}
	st = fu_struct_focal_moc_packet_header_new();
	fu_struct_focal_moc_packet_header_set_length(st, (guint16)data_sz + 1);
	fu_struct_focal_moc_packet_header_set_command(st, command);
	if (data_sz > 0)
		g_byte_array_append(st->buf, data, data_sz);
	fu_byte_array_append_uint8(st->buf, fu_xor8(st->buf->data + 1, st->buf->len - 1));

	/* success */
	return g_byte_array_ref(st->buf);
}

GByteArray *
fu_focal_moc_packet_parse(const guint8 *buf,
			  gsize bufsz,
			  gboolean allow_legacy_trailer,
			  guint8 *status,
			  GError **error)
{
	gsize packet_sz;
	guint16 length;
	guint8 bcc = 0;
	guint8 bcc_actual = 0;
	g_autoptr(FuStructFocalMocPacketHeader) st = NULL;
	g_autoptr(GByteArray) data = g_byte_array_new();

	g_return_val_if_fail(status != NULL, NULL);

	/* a zero-length USB read hands us a NULL buffer; this is device data,
	 * not a programmer error */
	if (buf == NULL || bufsz == 0) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INVALID_DATA,
				    "packet is empty");
		return NULL;
	}
	st = fu_struct_focal_moc_packet_header_parse(buf, bufsz, 0, error);
	if (st == NULL)
		return NULL;
	length = fu_struct_focal_moc_packet_header_get_length(st);
	if (length < 1) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INVALID_DATA,
				    "packet length excludes status");
		return NULL;
	}
	/* update protocol v1 firmware transmits one byte of stale buffer past the
	 * declared frame; accept exactly that one trailing byte there while still
	 * computing the BCC over the declared range only */
	packet_sz = (gsize)length + 4;
	if (bufsz != packet_sz && !(allow_legacy_trailer && bufsz == packet_sz + 1)) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INVALID_DATA,
			    "packet size invalid: got=0x%x expected=0x%x",
			    (guint)bufsz,
			    (guint)packet_sz);
		return NULL;
	}
	if (!fu_xor8_safe(buf, bufsz, 0x1, (gsize)length + 2, &bcc, error))
		return NULL;
	if (!fu_memread_uint8_safe(buf, bufsz, packet_sz - 1, &bcc_actual, error))
		return NULL;
	if (bcc != bcc_actual) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INVALID_DATA,
			    "packet BCC mismatch: got=0x%02x expected=0x%02x",
			    bcc_actual,
			    bcc);
		return NULL;
	}
	*status = fu_struct_focal_moc_packet_header_get_command(st);
	if (length > 1)
		g_byte_array_append(data, buf + FU_STRUCT_FOCAL_MOC_PACKET_HEADER_SIZE, length - 1);

	/* success */
	return g_steal_pointer(&data);
}

GByteArray *
fu_focal_moc_ymodem_build_soh(guint protocol_version,
			      gsize firmware_sz,
			      guint32 crc32,
			      GError **error)
{
	if (firmware_sz > G_MAXUINT32) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INVALID_DATA,
			    "firmware too large: 0x%x",
			    (guint)firmware_sz);
		return NULL;
	}
	if (protocol_version == 1) {
		g_autoptr(FuStructFocalMocSohV1) st = fu_struct_focal_moc_soh_v1_new();

		if (!fu_struct_focal_moc_soh_v1_set_filename(st, "app.bin", error))
			return NULL;
		fu_struct_focal_moc_soh_v1_set_firmware_size(st, (guint32)firmware_sz);
		fu_struct_focal_moc_soh_v1_set_crc32(st, crc32);
		return g_byte_array_ref(st->buf);
	}
	if (protocol_version == 2) {
		g_autoptr(FuStructFocalMocSohV2) st = fu_struct_focal_moc_soh_v2_new();

		if (!fu_struct_focal_moc_soh_v2_set_filename(st, "app.bin", error))
			return NULL;
		fu_struct_focal_moc_soh_v2_set_firmware_size(st, (guint32)firmware_sz);
		fu_struct_focal_moc_soh_v2_set_crc32(st, crc32);
		return g_byte_array_ref(st->buf);
	}
	g_set_error(error,
		    FWUPD_ERROR,
		    FWUPD_ERROR_INVALID_DATA,
		    "update protocol invalid: %u",
		    protocol_version);
	return NULL;
}

GByteArray *
fu_focal_moc_ymodem_build_data(guint protocol_version,
			       FuFocalMocFrame kind,
			       guint16 sequence,
			       const guint8 *data,
			       gsize data_sz,
			       GError **error)
{
	if (kind != FU_FOCAL_MOC_FRAME_STX && kind != FU_FOCAL_MOC_FRAME_EOT) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INVALID_DATA,
			    "data frame kind invalid: 0x%02x",
			    kind);
		return NULL;
	}
	if (data == NULL || data_sz > FU_FOCAL_MOC_YMODEM_DATA_SIZE) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INVALID_DATA,
			    "data frame size invalid: 0x%x",
			    (guint)data_sz);
		return NULL;
	}
	if (protocol_version == 1) {
		g_autoptr(FuStructFocalMocDataV1) st = fu_struct_focal_moc_data_v1_new();

		if (sequence > G_MAXUINT8) {
			g_set_error(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INVALID_DATA,
				    "protocol v1 sequence invalid: %u",
				    sequence);
			return NULL;
		}
		fu_struct_focal_moc_data_v1_set_kind(st, kind);
		fu_struct_focal_moc_data_v1_set_sequence(st, (guint8)sequence);
		if (!fu_struct_focal_moc_data_v1_set_data(st, data, data_sz, error))
			return NULL;
		return g_byte_array_ref(st->buf);
	}
	if (protocol_version == 2) {
		g_autoptr(FuStructFocalMocDataV2) st = fu_struct_focal_moc_data_v2_new();

		fu_struct_focal_moc_data_v2_set_kind(st, kind);
		fu_struct_focal_moc_data_v2_set_sequence(st, sequence);
		if (!fu_struct_focal_moc_data_v2_set_data(st, data, data_sz, error))
			return NULL;
		return g_byte_array_ref(st->buf);
	}
	g_set_error(error,
		    FWUPD_ERROR,
		    FWUPD_ERROR_INVALID_DATA,
		    "update protocol invalid: %u",
		    protocol_version);
	return NULL;
}

gchar *
fu_focal_moc_version_parse(const guint8 *buf, gsize bufsz, gboolean *is_bootloader, GError **error)
{
	const gchar *suffix;
	gboolean has_app;
	gboolean has_iap;
	g_autofree gchar *version_full = NULL;

	g_return_val_if_fail(is_bootloader != NULL, NULL);

	if (buf == NULL || bufsz == 0 || bufsz > FU_FOCAL_MOC_VERSION_MAX_SIZE) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INVALID_DATA,
			    "firmware version size invalid: 0x%x",
			    (guint)bufsz);
		return NULL;
	}
	version_full = g_strndup((const gchar *)buf, bufsz);
	if (version_full[0] == '\0' || !g_utf8_validate(version_full, -1, NULL)) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INVALID_DATA,
				    "firmware version is not valid text");
		return NULL;
	}
	/* the runtime and the bootloader share a USB product ID, so only the
	 * build string tells them apart */
	has_iap = g_strstr_len(version_full, -1, "_IAP_") != NULL;
	has_app = g_strstr_len(version_full, -1, "_APP_") != NULL;
	if (has_iap == has_app) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INVALID_DATA,
			    "cannot determine mode from version: %s",
			    version_full);
		return NULL;
	}
	*is_bootloader = has_iap;

	suffix = strrchr(version_full, '_');
	if (suffix == NULL || suffix[1] == '\0') {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INVALID_DATA,
			    "version has no build number: %s",
			    version_full);
		return NULL;
	}

	/* success */
	return g_strdup(suffix + 1);
}

gboolean
fu_focal_moc_fp_version_validate(const guint8 *buf, gsize bufsz, GError **error)
{
	gsize version_sz = 0;

	while (buf != NULL && version_sz < bufsz && buf[version_sz] != '\0')
		version_sz++;
	if (version_sz == 0) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INVALID_DATA,
				    "sensor firmware version is empty");
		return FALSE;
	}
	if (version_sz > FU_FOCAL_MOC_FP_VERSION_MAX_SIZE) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INVALID_DATA,
			    "sensor firmware version too long: 0x%x",
			    (guint)version_sz);
		return FALSE;
	}
	for (gsize i = 0; i < version_sz; i++) {
		if (!g_ascii_isprint((gchar)buf[i])) {
			g_set_error_literal(error,
					    FWUPD_ERROR,
					    FWUPD_ERROR_INVALID_DATA,
					    "sensor firmware version is not printable text");
			return FALSE;
		}
	}

	/* success */
	return TRUE;
}

gboolean
fu_focal_moc_iap_probe_required(gboolean is_bootloader, gboolean is_protocol_v2)
{
	/* the runtime firmware always implements the probed command whatever its
	 * status layout, and protocol v2 bootloaders already use the unified
	 * layout, so probing either would prove nothing */
	return is_bootloader && !is_protocol_v2;
}

gboolean
fu_focal_moc_iap_status_layout_from_probe(guint8 status,
					  const guint8 *buf,
					  gsize bufsz,
					  FuFocalMocIapStatusLayout *layout,
					  GError **error)
{
	g_return_val_if_fail(layout != NULL, FALSE);

	if (status == FU_FOCAL_MOC_STATUS_OK) {
		if (!fu_focal_moc_fp_version_validate(buf, bufsz, error)) {
			g_prefix_error_literal(error, "capability probe accepted: ");
			return FALSE;
		}
		*layout = FU_FOCAL_MOC_IAP_STATUS_LAYOUT_ALIGNED;
		return TRUE;
	}
	if (status == FU_FOCAL_MOC_STATUS_INVALID_COMMAND) {
		*layout = FU_FOCAL_MOC_IAP_STATUS_LAYOUT_LEGACY;
		return TRUE;
	}

	/* fail closed: without a decisive answer the status layout, and so every
	 * error the update would report, cannot be trusted */
	g_set_error(error,
		    FWUPD_ERROR,
		    FWUPD_ERROR_NOT_SUPPORTED,
		    "bootloader capability unknown: probe status=0x%02x",
		    status);
	return FALSE;
}

gboolean
fu_focal_moc_status_to_error(guint8 status,
			     const gchar *context,
			     FwupdError invalid_command_code,
			     gboolean status_aligned,
			     GByteArray *data,
			     GError **error)
{
	FwupdError code = FWUPD_ERROR_INTERNAL;
	g_autofree gchar *detail = NULL;

	switch (status) {
	case FU_FOCAL_MOC_STATUS_INVALID_PARAMETER:
		code = FWUPD_ERROR_INVALID_DATA;
		detail = g_strdup("invalid parameter");
		break;
	case FU_FOCAL_MOC_STATUS_TIMEOUT:
		code = FWUPD_ERROR_TIMED_OUT;
		detail = g_strdup("device timeout");
		break;
	case FU_FOCAL_MOC_STATUS_INVALID_COMMAND:
		code = invalid_command_code;
		if (code == FWUPD_ERROR_SIGNATURE_INVALID)
			detail = g_strdup("image validation or recovery failed");
		else if (code == FWUPD_ERROR_WRITE)
			detail = g_strdup("firmware transfer failed");
		else
			detail = g_strdup("invalid command");
		break;
	case FU_FOCAL_MOC_STATUS_NO_MEMORY:
		if (status_aligned)
			detail = g_strdup("device out of memory");
		break;
	case FU_FOCAL_MOC_STATUS_CHECK_ERROR:
		if (status_aligned) {
			code = FWUPD_ERROR_INVALID_DATA;
			detail = g_strdup("command check failed");
		}
		break;
	case FU_FOCAL_MOC_STATUS_EXECUTION_FAILURE:
		if (status_aligned)
			detail = g_strdup("command execution failed");
		break;
	case FU_FOCAL_MOC_STATUS_COMMAND_MASTER_KEY_INVALID:
		if (status_aligned) {
			code = FWUPD_ERROR_AUTH_FAILED;
			detail = g_strdup("command and master key invalid");
		}
		break;
	case FU_FOCAL_MOC_STATUS_MASTER_KEY_INVALID:
		if (status_aligned) {
			code = FWUPD_ERROR_AUTH_FAILED;
			detail = g_strdup("master key invalid");
		}
		break;
	case FU_FOCAL_MOC_STATUS_FIRMWARE_SEQUENCE: {
		guint16 expected = 0;
		code = FWUPD_ERROR_WRITE;
		if (data->len >= sizeof(expected) &&
		    fu_memread_uint16_safe(data->data, data->len, 0, &expected, G_BIG_ENDIAN, NULL))
			detail = g_strdup_printf("sequence mismatch expected=%u", expected);
		else
			detail = g_strdup("sequence mismatch");
		break;
	}
	case FU_FOCAL_MOC_STATUS_FIRMWARE_FLASH:
		code = FWUPD_ERROR_WRITE;
		detail = g_strdup("flash write failed");
		break;
	case FU_FOCAL_MOC_STATUS_FIRMWARE_NO_SESSION:
		code = FWUPD_ERROR_INTERNAL;
		detail = g_strdup("no firmware session");
		break;
	case FU_FOCAL_MOC_STATUS_FIRMWARE_VERIFY:
		code = FWUPD_ERROR_SIGNATURE_INVALID;
		detail = g_strdup("device rejected image; previous firmware restored");
		break;
	case FU_FOCAL_MOC_STATUS_FIRMWARE_RECOVER:
		code = FWUPD_ERROR_WRITE;
		detail = g_strdup("device stays in bootloader; run the update again");
		break;
	default:
		break;
	}
	if (detail == NULL) {
		code = FWUPD_ERROR_INTERNAL;
		detail = status_aligned ? g_strdup("unrecognized device status")
					: g_strdup("legacy IAP status");
	}
	g_set_error(error, FWUPD_ERROR, code, "%s: status=0x%02x (%s)", context, status, detail);
	return FALSE;
}
