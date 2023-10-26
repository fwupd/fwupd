/*
 * Copyright (C) 2023 Dell Technologies
 * Copyright (C) 2023 Mediatek Inc.
 *
 * SPDX-License-Identifier: LGPL-2.1+
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
	FuFirmwareClass parent_instance;
};

G_DEFINE_TYPE(FuMediatekScalerFirmware, fu_mediatek_scaler_firmware, FU_TYPE_FIRMWARE)

static gboolean
fu_mediatek_scaler_firmware_parse(FuFirmware *firmware,
				  GBytes *fw,
				  gsize offset,
				  FwupdInstallFlags flags,
				  GError **error)
{
	const guint8 *buf = g_bytes_get_data(fw, NULL);
	const gsize bufsz = g_bytes_get_size(fw);
	guint32 ver_tmp = 0x0;
	g_autofree gchar *fw_version = NULL;
	g_autofree gchar *fw_date = NULL;
	g_autofree gchar *fw_time = NULL;

	/* read version from firmware */
	if (!fu_memread_uint32_safe(buf,
				    bufsz,
				    MTK_FW_OFFSET_VERSION,
				    &ver_tmp,
				    G_LITTLE_ENDIAN,
				    error))
		return FALSE;
	fw_version = mediatek_scaler_device_version_to_string(ver_tmp);
	fu_firmware_set_version(firmware, fw_version);

	/* read timestamp from firmware */
	fw_date = fu_memstrsafe(buf,
				bufsz,
				MTK_FW_OFFSET_TIMESTAMP_DATE,
				MTK_FW_TIMESTAMP_DATE_SIZE,
				error);
	fw_time = fu_memstrsafe(buf,
				bufsz,
				MTK_FW_OFFSET_TIMESTAMP_TIME,
				MTK_FW_TIMESTAMP_TIME_SIZE,
				error);
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
	FuFirmwareClass *klass_firmware = FU_FIRMWARE_CLASS(klass);
	klass_firmware->parse = fu_mediatek_scaler_firmware_parse;
}

FuFirmware *
fu_mediatek_scaler_firmware_new(void)
{
	return FU_FIRMWARE(g_object_new(FU_TYPE_MEDIATEK_SCALER_FIRMWARE, NULL));
}
