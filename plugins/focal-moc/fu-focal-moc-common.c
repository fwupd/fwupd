/*
 * Copyright 2026 FocalTech Systems Co., Ltd.
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include "fu-focal-moc-common.h"

#define FU_FOCAL_MOC_FP_VERSION_MAX_SIZE 64

GByteArray *
fu_focal_moc_packet_new(guint8 command, const guint8 *buf, gsize bufsz, GError **error)
{
	g_autoptr(FuStructFocalMocPacketHeader) st = NULL;

	if (bufsz > 0 && buf == NULL) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INVALID_DATA,
				    "payload is missing");
		return NULL;
	}
	if (bufsz > G_MAXUINT16 - 1) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INVALID_DATA,
			    "payload too large: 0x%zx",
			    bufsz);
		return NULL;
	}
	st = fu_struct_focal_moc_packet_header_new();
	fu_struct_focal_moc_packet_header_set_length(st, (guint16)bufsz + 1);
	fu_struct_focal_moc_packet_header_set_command(st, command);
	if (bufsz > 0)
		g_byte_array_append(st->buf, buf, bufsz);
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
			    "packet size invalid: got=0x%zx expected=0x%zx",
			    bufsz,
			    packet_sz);
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
	if (status != NULL)
		*status = fu_struct_focal_moc_packet_header_get_command(st);
	if (length > 1)
		g_byte_array_append(data, buf + FU_STRUCT_FOCAL_MOC_PACKET_HEADER_SIZE, length - 1);

	/* success */
	return g_steal_pointer(&data);
}

GByteArray *
fu_focal_moc_ymodem_build_soh(gsize firmware_sz, guint32 crc32, GError **error)
{
	g_autoptr(FuStructFocalMocSohV1) st = fu_struct_focal_moc_soh_v1_new();

	if (firmware_sz > G_MAXUINT32) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INVALID_DATA,
			    "firmware too large: 0x%zx",
			    firmware_sz);
		return NULL;
	}
	if (!fu_struct_focal_moc_soh_v1_set_filename(st, "app.bin", error))
		return NULL;
	fu_struct_focal_moc_soh_v1_set_firmware_size(st, (guint32)firmware_sz);
	fu_struct_focal_moc_soh_v1_set_crc32(st, crc32);
	return g_byte_array_ref(st->buf);
}

GByteArray *
fu_focal_moc_ymodem_build_soh_v2(gsize firmware_sz, guint32 crc32, GError **error)
{
	g_autoptr(FuStructFocalMocSohV2) st = fu_struct_focal_moc_soh_v2_new();

	if (firmware_sz > G_MAXUINT32) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INVALID_DATA,
			    "firmware too large: 0x%zx",
			    firmware_sz);
		return NULL;
	}
	if (!fu_struct_focal_moc_soh_v2_set_filename(st, "app.bin", error))
		return NULL;
	fu_struct_focal_moc_soh_v2_set_firmware_size(st, (guint32)firmware_sz);
	fu_struct_focal_moc_soh_v2_set_crc32(st, crc32);
	return g_byte_array_ref(st->buf);
}

GByteArray *
fu_focal_moc_ymodem_build_data(FuFocalMocFrame kind,
			       guint16 sequence,
			       const guint8 *buf,
			       gsize bufsz,
			       GError **error)
{
	g_autoptr(FuStructFocalMocDataV1) st = fu_struct_focal_moc_data_v1_new();

	if (kind != FU_FOCAL_MOC_FRAME_STX && kind != FU_FOCAL_MOC_FRAME_EOT) {
		const gchar *kindstr = fu_focal_moc_frame_to_string(kind);
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INVALID_DATA,
			    "data frame kind invalid: %s",
			    kindstr != NULL ? kindstr : "unknown");
		return NULL;
	}
	if (buf == NULL || bufsz > FU_FOCAL_MOC_YMODEM_DATA_SIZE) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INVALID_DATA,
			    "data frame size invalid: 0x%zx",
			    bufsz);
		return NULL;
	}
	/* the frame number is a single byte on the wire */
	if (sequence > G_MAXUINT8) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INVALID_DATA,
			    "frame sequence invalid: %u",
			    sequence);
		return NULL;
	}
	fu_struct_focal_moc_data_v1_set_kind(st, kind);
	fu_struct_focal_moc_data_v1_set_sequence(st, (guint8)sequence);
	if (!fu_struct_focal_moc_data_v1_set_data(st, buf, bufsz, error))
		return NULL;
	return g_byte_array_ref(st->buf);
}

GByteArray *
fu_focal_moc_ymodem_build_data_v2(FuFocalMocFrame kind,
				  guint16 sequence,
				  const guint8 *buf,
				  gsize bufsz,
				  GError **error)
{
	g_autoptr(FuStructFocalMocDataV2) st = fu_struct_focal_moc_data_v2_new();

	if (kind != FU_FOCAL_MOC_FRAME_STX && kind != FU_FOCAL_MOC_FRAME_EOT) {
		const gchar *kindstr = fu_focal_moc_frame_to_string(kind);
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INVALID_DATA,
			    "data frame kind invalid: %s",
			    kindstr != NULL ? kindstr : "unknown");
		return NULL;
	}
	if (buf == NULL || bufsz > FU_FOCAL_MOC_YMODEM_DATA_SIZE) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INVALID_DATA,
			    "data frame size invalid: 0x%zx",
			    bufsz);
		return NULL;
	}
	fu_struct_focal_moc_data_v2_set_kind(st, kind);
	fu_struct_focal_moc_data_v2_set_sequence(st, sequence);
	if (!fu_struct_focal_moc_data_v2_set_data(st, buf, bufsz, error))
		return NULL;
	return g_byte_array_ref(st->buf);
}

gchar *
fu_focal_moc_version_parse(const guint8 *buf, gsize bufsz, gboolean *is_bootloader, GError **error)
{
	const gchar *suffix;
	gboolean has_app;
	gboolean has_iap;
	g_autofree gchar *version_full = NULL;

	if (buf == NULL || bufsz == 0 || bufsz > FU_FOCAL_MOC_VERSION_MAX_SIZE) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INVALID_DATA,
			    "firmware version size invalid: 0x%zx",
			    bufsz);
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
	if (is_bootloader != NULL)
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
	g_autofree gchar *version = NULL;

	if (buf != NULL && bufsz > 0)
		version = fu_strsafe((const gchar *)buf, bufsz);
	if (version == NULL) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INVALID_DATA,
				    "sensor firmware version is empty");
		return FALSE;
	}
	if (strlen(version) > FU_FOCAL_MOC_FP_VERSION_MAX_SIZE) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INVALID_DATA,
			    "sensor firmware version too long: 0x%zx",
			    strlen(version));
		return FALSE;
	}
	/* fu_strsafe() maps anything unprintable to a dot, so only the raw bytes
	 * can tell a substitution apart from a version that really has one */
	if (strchr(version, '.') != NULL) {
		for (gsize i = 0; i < bufsz && buf[i] != '\0'; i++) {
			if (!g_ascii_isprint((gchar)buf[i])) {
				g_set_error_literal(
				    error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INVALID_DATA,
				    "sensor firmware version is not printable text");
				return FALSE;
			}
		}
	}

	/* success */
	return TRUE;
}

gboolean
fu_focal_moc_iap_probe_required(gboolean is_bootloader)
{
	/* the runtime firmware always implements the probed command whatever its
	 * status layout, so probing it would prove nothing */
	return is_bootloader;
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
	g_return_val_if_fail(data != NULL, FALSE);

	/* these codes only have a meaning in the unified layout; a legacy
	 * bootloader reuses them for unrelated conditions */
	if (!status_aligned) {
		switch (status) {
		case FU_FOCAL_MOC_STATUS_NO_MEMORY:
		case FU_FOCAL_MOC_STATUS_CHECK_ERROR:
		case FU_FOCAL_MOC_STATUS_EXECUTION_FAILURE:
		case FU_FOCAL_MOC_STATUS_COMMAND_MASTER_KEY_INVALID:
		case FU_FOCAL_MOC_STATUS_MASTER_KEY_INVALID:
			g_set_error(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INTERNAL,
				    "%s: status=0x%02x (legacy IAP status)",
				    context,
				    status);
			return FALSE;
		default:
			break;
		}
	}
	switch (status) {
	case FU_FOCAL_MOC_STATUS_INVALID_PARAMETER:
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INVALID_DATA,
			    "%s: status=0x%02x (invalid parameter)",
			    context,
			    status);
		return FALSE;
	case FU_FOCAL_MOC_STATUS_TIMEOUT:
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_TIMED_OUT,
			    "%s: status=0x%02x (device timeout)",
			    context,
			    status);
		return FALSE;
	case FU_FOCAL_MOC_STATUS_INVALID_COMMAND:
		if (invalid_command_code == FWUPD_ERROR_SIGNATURE_INVALID) {
			g_set_error(error,
				    FWUPD_ERROR,
				    invalid_command_code,
				    "%s: status=0x%02x (image validation or recovery failed)",
				    context,
				    status);
			return FALSE;
		}
		if (invalid_command_code == FWUPD_ERROR_WRITE) {
			g_set_error(error,
				    FWUPD_ERROR,
				    invalid_command_code,
				    "%s: status=0x%02x (firmware transfer failed)",
				    context,
				    status);
			return FALSE;
		}
		g_set_error(error,
			    FWUPD_ERROR,
			    invalid_command_code,
			    "%s: status=0x%02x (invalid command)",
			    context,
			    status);
		return FALSE;
	case FU_FOCAL_MOC_STATUS_NO_MEMORY:
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INTERNAL,
			    "%s: status=0x%02x (device out of memory)",
			    context,
			    status);
		return FALSE;
	case FU_FOCAL_MOC_STATUS_CHECK_ERROR:
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INVALID_DATA,
			    "%s: status=0x%02x (command check failed)",
			    context,
			    status);
		return FALSE;
	case FU_FOCAL_MOC_STATUS_EXECUTION_FAILURE:
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INTERNAL,
			    "%s: status=0x%02x (command execution failed)",
			    context,
			    status);
		return FALSE;
	case FU_FOCAL_MOC_STATUS_COMMAND_MASTER_KEY_INVALID:
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_AUTH_FAILED,
			    "%s: status=0x%02x (command and master key invalid)",
			    context,
			    status);
		return FALSE;
	case FU_FOCAL_MOC_STATUS_MASTER_KEY_INVALID:
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_AUTH_FAILED,
			    "%s: status=0x%02x (master key invalid)",
			    context,
			    status);
		return FALSE;
	case FU_FOCAL_MOC_STATUS_FIRMWARE_SEQUENCE: {
		guint16 expected = 0;
		if (data->len >= sizeof(expected) && fu_memread_uint16_safe(data->data,
									    data->len,
									    0,
									    &expected,
									    G_BIG_ENDIAN,
									    NULL)) {
			g_set_error(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_WRITE,
				    "%s: status=0x%02x (sequence mismatch expected=%u)",
				    context,
				    status,
				    expected);
			return FALSE;
		}
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_WRITE,
			    "%s: status=0x%02x (sequence mismatch)",
			    context,
			    status);
		return FALSE;
	}
	case FU_FOCAL_MOC_STATUS_FIRMWARE_FLASH:
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_WRITE,
			    "%s: status=0x%02x (flash write failed)",
			    context,
			    status);
		return FALSE;
	case FU_FOCAL_MOC_STATUS_FIRMWARE_NO_SESSION:
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INTERNAL,
			    "%s: status=0x%02x (no firmware session)",
			    context,
			    status);
		return FALSE;
	case FU_FOCAL_MOC_STATUS_FIRMWARE_VERIFY:
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_SIGNATURE_INVALID,
			    "%s: status=0x%02x (device rejected image; previous firmware restored)",
			    context,
			    status);
		return FALSE;
	case FU_FOCAL_MOC_STATUS_FIRMWARE_RECOVER:
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_WRITE,
			    "%s: status=0x%02x (device stays in bootloader; run the update again)",
			    context,
			    status);
		return FALSE;
	default:
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INTERNAL,
			    "%s: status=0x%02x (%s)",
			    context,
			    status,
			    status_aligned ? "unrecognized device status" : "legacy IAP status");
		return FALSE;
	}
}
