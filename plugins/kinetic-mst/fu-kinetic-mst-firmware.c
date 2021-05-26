/*
 * Copyright (C) 2021 Richard Hughes <richard@hughsie.com>
 * Copyright (C) 2021 Jeffrey Lin <jlin@kinet-ic.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include "fu-common.h"

#include "fu-kinetic-mst-connection.h"
#include "fu-kinetic-mst-firmware.h"

struct _FuKineticMstFirmware {
    FuFirmwareClass parent_instance;
};

G_DEFINE_TYPE (FuKineticMstFirmware, fu_kinetic_mst_firmware, FU_TYPE_FIRMWARE)

#define HEADER_LEN_ISP_DRV_SIZE 4

static void
fu_kinetic_mst_firmware_to_string (FuFirmware *firmware, guint idt, GString *str)
{
}

static gboolean
fu_kinetic_mst_firmware_parse(FuFirmware *firmware,
                              GBytes *fw,
                              guint64 addr_start,
                              guint64 addr_end,
                              FwupdInstallFlags flags,
                              GError **error)
{
	const guint8 *buf;
	gsize bufsz;
	guint32 isp_drv_payload_size = 0, app_fw_payload_size = 0;
	g_autoptr(GBytes) isp_drv_payload = NULL;
	g_autoptr(GBytes) app_fw_payload = NULL;
	g_autoptr(FuFirmwareImage) isp_drv_img = NULL;
	g_autoptr(FuFirmwareImage) app_fw_img = NULL;

    /* Parse firmware according to Kinetic's FW image format
     * FW binary = 4 bytes header(Little-Endian) + ISP driver + app FW
     * 4 bytes: size of ISP driver
     */
    buf = g_bytes_get_data(fw, &bufsz);
    if (!fu_common_read_uint32_safe(buf, bufsz, 0,
    				                &isp_drv_payload_size, G_LITTLE_ENDIAN, error))
    {
		return FALSE;
	}
    g_debug("ISP driver payload size: %u bytes", isp_drv_payload_size);

    app_fw_payload_size = g_bytes_get_size(fw) - HEADER_LEN_ISP_DRV_SIZE - isp_drv_payload_size;
    g_debug("App FW payload size: %u bytes", app_fw_payload_size);

    // Add ISP driver as a new image into firmware
    isp_drv_payload = g_bytes_new_from_bytes(fw, HEADER_LEN_ISP_DRV_SIZE, isp_drv_payload_size);
    isp_drv_img = fu_firmware_image_new(isp_drv_payload);
    fu_firmware_add_image(firmware, isp_drv_img);

    // Add app FW as a new image into firmware
    app_fw_payload = g_bytes_new_from_bytes(fw, HEADER_LEN_ISP_DRV_SIZE + isp_drv_payload_size, app_fw_payload_size);
    app_fw_img = fu_firmware_image_new(app_fw_payload);
    fu_firmware_add_image(firmware, app_fw_img);

	return TRUE;
}

static void
fu_kinetic_mst_firmware_init (FuKineticMstFirmware *self)
{
}

static void
fu_kinetic_mst_firmware_class_init (FuKineticMstFirmwareClass *klass)
{
    FuFirmwareClass *klass_firmware = FU_FIRMWARE_CLASS(klass);
    klass_firmware->parse = fu_kinetic_mst_firmware_parse;
    klass_firmware->to_string = fu_kinetic_mst_firmware_to_string;
}

FuFirmware *
fu_kinetic_mst_firmware_new (void)
{
	return FU_FIRMWARE(g_object_new(FU_TYPE_KINETIC_MST_FIRMWARE, NULL));
}

