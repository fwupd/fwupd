/*
 * Copyright 2023 Dell Technologies
 * Copyright 2023 Mediatek Inc.
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include "fu-mediatek-scaler-common.h"
#include "fu-mediatek-scaler-firmware.h"

#define MTK_FW_OFFSET_VERSION	     0x7118
#define MTK_FW_OFFSET_TIMESTAMP_DATE 0x7200
#define MTK_FW_OFFSET_TIMESTAMP_TIME 0x720c
#define MTK_FW_TIMESTAMP_DATE_SIZE   11
#define MTK_FW_TIMESTAMP_TIME_SIZE   8

struct _FuMediatekScalerFirmware {
	FuFirmware parent_instance;
};

G_DEFINE_TYPE(FuMediatekScalerFirmware, fu_mediatek_scaler_firmware, FU_TYPE_FIRMWARE)

static gboolean
fu_mediatek_scaler_firmware_parse(FuFirmware *firmware,
				  GInputStream *stream,
				  FuFirmwareParseFlags flags,
				  GError **error)
{
	guint32 ver_tmp = 0x0;
	guint8 buf_date[MTK_FW_TIMESTAMP_DATE_SIZE] = {0};
	guint8 buf_time[MTK_FW_TIMESTAMP_TIME_SIZE] = {0};
	g_autofree gchar *fw_version = NULL;
	g_autofree gchar *fw_date = NULL;
	g_autofree gchar *fw_time = NULL;

	/* read version from firmware */
	if (!fu_input_stream_read_u32(stream,
				      MTK_FW_OFFSET_VERSION,
				      &ver_tmp,
				      G_LITTLE_ENDIAN,
				      error))
		return FALSE;
	fw_version = fu_mediatek_scaler_version_to_string(ver_tmp);
	fu_firmware_set_version(firmware, fw_version);

	/* read timestamp from firmware */
	if (!fu_input_stream_read_safe(stream,
				       buf_date,
				       sizeof(buf_date),
				       0x0,
				       MTK_FW_OFFSET_TIMESTAMP_DATE,
				       sizeof(buf_date),
				       error))
		return FALSE;
	fw_date = fu_strsafe((const gchar *)buf_date, sizeof(buf_date));
	if (!fu_input_stream_read_safe(stream,
				       buf_time,
				       sizeof(buf_time),
				       0x0,
				       MTK_FW_OFFSET_TIMESTAMP_TIME,
				       sizeof(buf_time),
				       error))
		return FALSE;
	fw_time = fu_strsafe((const gchar *)buf_time, sizeof(buf_time));

	g_info("firmware timestamp: %s, %s", fw_time, fw_date);
	return TRUE;
}

static void
fu_mediatek_scaler_firmware_init(FuMediatekScalerFirmware *self)
{
}

static void
fu_mediatek_scaler_firmware_class_init(FuMediatekScalerFirmwareClass *klass)
{
	FuFirmwareClass *firmware_class = FU_FIRMWARE_CLASS(klass);
	firmware_class->parse = fu_mediatek_scaler_firmware_parse;
}

FuFirmware *
fu_mediatek_scaler_firmware_new(void)
{
	return FU_FIRMWARE(g_object_new(FU_TYPE_MEDIATEK_SCALER_FIRMWARE, NULL));
}
