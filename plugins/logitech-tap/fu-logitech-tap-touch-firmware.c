/*
 * Copyright 2024 Logitech, Inc.
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include "fu-logitech-tap-touch-common.h"
#include "fu-logitech-tap-touch-firmware.h"

struct _FuLogitechTapTouchFirmware {
	FuFirmware parent_instance;
	guint32 mapping_version;
	guint16 fw_ic_name;
	guint32 protocol_version;
	guint16 ap_checksum;
	guint16 df_checksum;
};

G_DEFINE_TYPE(FuLogitechTapTouchFirmware, fu_logitech_tap_touch_firmware, FU_TYPE_FIRMWARE)

/*
 * mapping info addr in firmware file 0x2020:
 * 3 bytes mapping version
 * 3 bytes protocol version
 * 6 bytes ic name
 */
#define TAP_TOUCH_MAPPING_INFO_ADDR 0x2020

#define TAP_TOUCH_AP_START 0x2000
#define TAP_TOUCH_DF_START 0xF000

guint16
fu_logitech_tap_touch_firmware_get_ap_checksum(FuLogitechTapTouchFirmware *self)
{
	g_return_val_if_fail(FU_LOGITECH_TAP_TOUCH_FIRMWARE(self), 0);
	return self->ap_checksum;
}

guint16
fu_logitech_tap_touch_firmware_get_df_checksum(FuLogitechTapTouchFirmware *self)
{
	g_return_val_if_fail(FU_LOGITECH_TAP_TOUCH_FIRMWARE(self), 0);
	return self->df_checksum;
}

static void
fu_logitech_tap_touch_firmware_export(FuFirmware *firmware,
				      FuFirmwareExportFlags flags,
				      XbBuilderNode *bn)
{
	FuLogitechTapTouchFirmware *self = FU_LOGITECH_TAP_TOUCH_FIRMWARE(firmware);
	fu_xmlb_builder_insert_kx(bn, "fw_ic_name", self->fw_ic_name);
	fu_xmlb_builder_insert_kx(bn, "protocol_version", self->protocol_version);
	if (flags & FU_FIRMWARE_EXPORT_FLAG_INCLUDE_DEBUG) {
		fu_xmlb_builder_insert_kx(bn, "mapping_version", self->mapping_version);
		fu_xmlb_builder_insert_kx(bn, "ap_checksum", self->ap_checksum);
		fu_xmlb_builder_insert_kx(bn, "df_checksum", self->df_checksum);
	}
}

static gboolean
fu_logitech_tap_touch_firmware_validate(FuFirmware *firmware,
					GInputStream *stream,
					gsize offset,
					GError **error)
{
	gsize streamsz = 0;

	/* validate firmware file size, typically between 60k to 75k */
	if (!fu_input_stream_size(stream, &streamsz, error))
		return FALSE;
	if (streamsz > FU_LOGITECH_TAP_TOUCH_MAX_FW_FILE_SIZE ||
	    streamsz < FU_LOGITECH_TAP_TOUCH_MIN_FW_FILE_SIZE) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INVALID_FILE,
			    "unexpected firmware size, got 0x%x expected 0x%x",
			    (guint32)streamsz,
			    (guint32)FU_LOGITECH_TAP_TOUCH_MAX_FW_FILE_SIZE);
		return FALSE;
	}

	/* success */
	return TRUE;
}

static gboolean
fu_logitech_tap_touch_firmware_calculate_ap_crc_cb(const guint8 *buf,
						   gsize bufsz,
						   gpointer user_data,
						   GError **error)
{
	guint32 *ap_check = (guint32 *)user_data;
	const guint16 ap_polynomial = 0x8408;

	for (gsize i = 0; i < bufsz; i++) {
		*ap_check ^= buf[i];
		for (guint8 idx = 0; idx < 8; idx++) {
			if (*ap_check & 0x01)
				*ap_check = (*ap_check >> 1) ^ ap_polynomial;
			else
				*ap_check = *ap_check >> 1;
		}
	}
	return TRUE;
}

static gboolean
fu_logitech_tap_touch_firmware_calculate_basic_cb(const guint8 *buf,
						  gsize bufsz,
						  gpointer user_data,
						  GError **error)
{
	guint32 *df_check = (guint32 *)user_data;
	for (gsize i = 0; i < bufsz; i++)
		*df_check += buf[i];
	return TRUE;
}

static gboolean
fu_logitech_tap_touch_firmware_parse(FuFirmware *firmware,
				     GInputStream *stream,
				     gsize offset,
				     FwupdInstallFlags flags,
				     GError **error)
{
	FuLogitechTapTouchFirmware *self = FU_LOGITECH_TAP_TOUCH_FIRMWARE(firmware);
	const guint32 ap_start = TAP_TOUCH_AP_START;
	const guint32 df_start = TAP_TOUCH_DF_START;
	gsize ap_end_offset = 0;
	gsize streamsz;
	guint32 ap_end;
	guint32 df_end;
	guint32 version_raw_major = 0;
	guint32 version_raw_minor = 0;
	guint64 version_raw;
	guint8 protocol_id = 0;
	g_autoptr(FuFirmware) ap_img = fu_firmware_new();
	g_autoptr(FuFirmware) df_img = fu_firmware_new();
	g_autoptr(GInputStream) ap_stream = NULL;
	/* temp stream to calculate ap crc, it is few bytes smaller */
	g_autoptr(GInputStream) ap_stream_crc = NULL;
	g_autoptr(GInputStream) df_stream = NULL;
	const gchar *image_end_magic =
	    "\xFF\xFF\xFF\xFF\xFF\xFF\xFF\xFF\xFF\xFF\xFF\xFF\xFF\xFF\xFF\xFFILITek AP CRC   ";

	if (!fu_input_stream_size(stream, &streamsz, error))
		return FALSE;

	/* file firmware version */
	if (!fu_input_stream_read_u32(stream, 0x2030, &version_raw_major, G_LITTLE_ENDIAN, error))
		return FALSE;
	if (!fu_input_stream_read_u32(stream, 0xF004, &version_raw_minor, G_BIG_ENDIAN, error))
		return FALSE;
	version_raw = (((guint64)version_raw_major) << 32) | version_raw_minor;
	fu_firmware_set_version_raw(firmware, version_raw);

	/* mapping info: mapping version, protocol version, ic name */
	if (!fu_input_stream_read_u24(stream,
				      TAP_TOUCH_MAPPING_INFO_ADDR,
				      &self->mapping_version,
				      G_LITTLE_ENDIAN,
				      error))
		return FALSE;
	if (!fu_input_stream_read_u24(stream,
				      TAP_TOUCH_MAPPING_INFO_ADDR + 3,
				      &self->protocol_version,
				      G_LITTLE_ENDIAN,
				      error))
		return FALSE;

	/* read and validate protocol id and ic name */
	if (!fu_input_stream_read_u8(stream, TAP_TOUCH_MAPPING_INFO_ADDR + 5, &protocol_id, error))
		return FALSE;
	if (!fu_input_stream_read_u16(stream,
				      TAP_TOUCH_MAPPING_INFO_ADDR + 6,
				      &self->fw_ic_name,
				      G_LITTLE_ENDIAN,
				      error))
		return FALSE;
	if (protocol_id != FU_LOGITECH_TAP_TOUCH_SUPPORTED_PROTOCOL_VERSION ||
	    self->fw_ic_name != FU_LOGITECH_TAP_TOUCH_IC_NAME) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INVALID_DATA,
			    "failed to validate firmware, "
			    "protocol version: %x, fw ic name:%x",
			    protocol_id,
			    self->fw_ic_name);
		return FALSE;
	}

	/* read and validate magic tag, determine ap block end location */
	df_end = streamsz;
	if (!fu_input_stream_find(stream,
				  (const guint8 *)image_end_magic,
				  strlen(image_end_magic),
				  &ap_end_offset,
				  error)) {
		g_prefix_error(error, "failed to find anchor: ");
		return FALSE;
	}
	ap_end = ap_end_offset + 32 + 2;

	/* get crc for pflash (AP) */
	ap_stream = fu_partial_input_stream_new(stream, ap_start, ap_end - ap_start, error);
	if (ap_stream == NULL)
		return FALSE;
	ap_stream_crc =
	    fu_partial_input_stream_new(stream, ap_start, (ap_end - 2) - ap_start, error);
	if (ap_stream_crc == NULL)
		return FALSE;
	if (!fu_input_stream_chunkify(ap_stream_crc,
				      fu_logitech_tap_touch_firmware_calculate_ap_crc_cb,
				      &self->ap_checksum,
				      error))
		return FALSE;
	fu_firmware_set_id(ap_img, "ap");
	fu_firmware_set_offset(ap_img, ap_end);
	if (!fu_firmware_set_stream(ap_img, ap_stream, error))
		return FALSE;
	fu_firmware_add_image(firmware, ap_img);

	/* calculate basic checksum for dataflash (DF) */
	df_stream = fu_partial_input_stream_new(stream, df_start, df_end - df_start, error);
	if (df_stream == NULL)
		return FALSE;
	if (!fu_input_stream_chunkify(df_stream,
				      fu_logitech_tap_touch_firmware_calculate_basic_cb,
				      &self->df_checksum,
				      error))
		return FALSE;
	fu_firmware_set_id(df_img, "df");
	fu_firmware_set_offset(df_img, df_end);
	if (!fu_firmware_set_stream(df_img, df_stream, error))
		return FALSE;
	fu_firmware_add_image(firmware, df_img);

	/* success */
	return TRUE;
}

static gchar *
fu_logitech_tap_touch_firmware_convert_version(FuFirmware *firmware, guint64 version_raw)
{
	/* convert 8 byte version in to human readable format. e.g. convert
	 * 0x0600000003000004 into 6000.3004*/
	return g_strdup_printf("%01x%01x%01x%01x.%01x%01x%01x%01x",
			       (guint)((version_raw >> 56) & 0xFF),
			       (guint)((version_raw >> 48) & 0xFF),
			       (guint)((version_raw >> 40) & 0xFF),
			       (guint)((version_raw >> 32) & 0xFF),
			       (guint)((version_raw >> 24) & 0xFF),
			       (guint)((version_raw >> 16) & 0xFF),
			       (guint)((version_raw >> 8) & 0xFF),
			       (guint)((version_raw >> 0) & 0xFF));
}

static void
fu_logitech_tap_touch_firmware_init(FuLogitechTapTouchFirmware *self)
{
	fu_firmware_set_version_format(FU_FIRMWARE(self), FWUPD_VERSION_FORMAT_PAIR);
}

static void
fu_logitech_tap_touch_firmware_class_init(FuLogitechTapTouchFirmwareClass *klass)
{
	FuFirmwareClass *firmware_class = FU_FIRMWARE_CLASS(klass);
	firmware_class->convert_version = fu_logitech_tap_touch_firmware_convert_version;
	firmware_class->parse = fu_logitech_tap_touch_firmware_parse;
	firmware_class->export = fu_logitech_tap_touch_firmware_export;
	firmware_class->validate = fu_logitech_tap_touch_firmware_validate;
}
