/*
 * Copyright (C) 2021 Richard Hughes <richard@hughsie.com>
 * Copyright (C) 2021 Jeffrey Lin <jlin@kinet-ic.com>
 * Copyright (C) 2022 Hai Su <hsu@kinet-ic.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include "fu-kinetic-dp-secure-device.h"
#include "fu-kinetic-dp-secure-firmware.h"

struct _FuKineticDpSecureFirmware {
	FuFirmwareClass parent_instance;
};

typedef struct {
	FuKineticDpChip chip_id;
	guint32 isp_drv_size;
	guint32 esm_payload_size;
	guint32 arm_app_code_size;
	guint16 app_init_data_size;
	guint16 cmdb_block_size;
	gboolean esm_xip_enabled;
} FuKineticDpSecureFirmwarePrivate;

G_DEFINE_TYPE_WITH_PRIVATE(FuKineticDpSecureFirmware,
			   fu_kinetic_dp_secure_firmware,
			   FU_TYPE_FIRMWARE)
#define GET_PRIVATE(o) (fu_kinetic_dp_secure_firmware_get_instance_private(o))

#define HEADER_LEN_ISP_DRV_SIZE 4
#define APP_ID_STR_LEN		4

static void
fu_kinetic_dp_secure_firmware_export(FuFirmware *firmware,
				     FuFirmwareExportFlags flags,
				     XbBuilderNode *bn)
{
	FuKineticDpSecureFirmware *self = FU_KINETIC_DP_SECURE_FIRMWARE(firmware);
	FuKineticDpSecureFirmwarePrivate *priv = GET_PRIVATE(self);

	fu_xmlb_builder_insert_kv(bn, "chip_id", fu_kinetic_dp_chip_to_string(priv->chip_id));
	fu_xmlb_builder_insert_kx(bn, "isp_drv_size", priv->isp_drv_size);
	fu_xmlb_builder_insert_kx(bn, "esm_payload_size", priv->esm_payload_size);
	fu_xmlb_builder_insert_kx(bn, "arm_app_code_size", priv->arm_app_code_size);
	fu_xmlb_builder_insert_kx(bn, "app_init_data_size", priv->app_init_data_size);
	fu_xmlb_builder_insert_kx(bn, "cmdb_block_size", priv->cmdb_block_size);
	fu_xmlb_builder_insert_kb(bn, "esm_xip_enabled", priv->esm_xip_enabled);
}

static gboolean
fu_kinetic_dp_secure_firmware_parse_chip_id(GBytes *fw,
					    FuKineticDpChip *chip_id,
					    gboolean *esm_xip_enabled,
					    GError **error)
{
	gsize bufsz = 0;
	const guint8 *buf = g_bytes_get_data(fw, &bufsz);
	const struct {
		FuKineticDpChip chip_id;
		guint32 offset;
		const gchar *app_id;
		gboolean esm_xip_enabled;
	} map[] = {
	    {FU_KINETIC_DP_CHIP_JAGUAR_5000, 0x0FFFE4UL, "JAGR", FALSE},  /* 1024KB */
	    {FU_KINETIC_DP_CHIP_JAGUAR_5000, 0x0A7036UL, "JAGR", FALSE},  /* 670KB ANZU */
	    {FU_KINETIC_DP_CHIP_JAGUAR_5000, 0x0FFFE4UL, "JAGX", TRUE},	  /* 1024KB (640KB) */
	    {FU_KINETIC_DP_CHIP_JAGUAR_5000, 0x0E7036UL, "JAGX", TRUE},	  /* 670KB ANZU (640KB) */
	    {FU_KINETIC_DP_CHIP_MUSTANG_5200, 0x0FFFE4UL, "MSTG", FALSE}, /* 1024KB */
	    {FU_KINETIC_DP_CHIP_MUSTANG_5200, 0x0A7036UL, "MSTG", FALSE}, /* 670KB ANZU */
	    {FU_KINETIC_DP_CHIP_MUSTANG_5200, 0x0FFFE4UL, "MSTX", TRUE},  /* 1024KB (640KB) */
	    {FU_KINETIC_DP_CHIP_MUSTANG_5200, 0x0E7036UL, "MSTX", TRUE},  /* 670KB ANZU (640KB) */
	};

	for (guint32 i = 0; i < G_N_ELEMENTS(map); i++) {
		if (fu_memcmp_safe(buf,
				   bufsz,
				   map[i].offset,
				   (const guint8 *)map[i].app_id,
				   APP_ID_STR_LEN,
				   0x0,
				   APP_ID_STR_LEN,
				   NULL)) {
			*chip_id = map[i].chip_id;
			*esm_xip_enabled = map[i].esm_xip_enabled;
			return TRUE;
		}
	}

	/* failed */
	g_set_error_literal(error,
			    G_IO_ERROR,
			    G_IO_ERROR_NOT_SUPPORTED,
			    "no valid Chip ID is found in the firmware");
	return FALSE;
}

guint32
fu_kinetic_dp_secure_firmware_get_esm_payload_size(FuKineticDpSecureFirmware *self)
{
	FuKineticDpSecureFirmwarePrivate *priv = GET_PRIVATE(self);
	g_return_val_if_fail(FU_KINETIC_DP_SECURE_FIRMWARE(self), 0);
	return priv->esm_payload_size;
}

guint32
fu_kinetic_dp_secure_firmware_get_arm_app_code_size(FuKineticDpSecureFirmware *self)
{
	FuKineticDpSecureFirmwarePrivate *priv = GET_PRIVATE(self);
	g_return_val_if_fail(FU_KINETIC_DP_SECURE_FIRMWARE(self), 0);
	return priv->arm_app_code_size;
}

guint16
fu_kinetic_dp_secure_firmware_get_app_init_data_size(FuKineticDpSecureFirmware *self)
{
	FuKineticDpSecureFirmwarePrivate *priv = GET_PRIVATE(self);
	g_return_val_if_fail(FU_KINETIC_DP_SECURE_FIRMWARE(self), 0);
	return priv->app_init_data_size;
}

guint16
fu_kinetic_dp_secure_firmware_get_cmdb_block_size(FuKineticDpSecureFirmware *self)
{
	FuKineticDpSecureFirmwarePrivate *priv = GET_PRIVATE(self);
	g_return_val_if_fail(FU_KINETIC_DP_SECURE_FIRMWARE(self), 0);
	return priv->cmdb_block_size;
}

gboolean
fu_kinetic_dp_secure_firmware_get_esm_xip_enabled(FuKineticDpSecureFirmware *self)
{
	FuKineticDpSecureFirmwarePrivate *priv = GET_PRIVATE(self);
	g_return_val_if_fail(FU_KINETIC_DP_SECURE_FIRMWARE(self), FALSE);
	return priv->esm_xip_enabled;
}

static gboolean
fu_kinetic_dp_secure_device_parse_app_fw(FuKineticDpSecureFirmware *self,
					 GBytes *fw,
					 GError **error)
{
	FuKineticDpSecureFirmwarePrivate *priv = GET_PRIVATE(self);
	guint32 app_code_block_size;
	guint32 std_fw_ver = 0;
	g_autoptr(GByteArray) st = NULL;

	/* sanity check */
	if (g_bytes_get_size(fw) != STD_FW_PAYLOAD_SIZE) {
		g_set_error(error,
			    G_IO_ERROR,
			    G_IO_ERROR_INVALID_DATA,
			    "firmware payload size (0x%x) is not valid",
			    (guint)g_bytes_get_size(fw));
		return FALSE;
	}

	if (priv->esm_xip_enabled) {
		app_code_block_size = APP_CODE_EXTEND_BLOCK_SIZE;
	} else {
		app_code_block_size = APP_CODE_NORMAL_BLOCK_SIZE;
	}

	/* get FW info embedded in FW file */
	st = fu_struct_kinetic_dp_jaguar_footer_parse_bytes(fw, SPI_APP_ID_DATA_START, error);
	if (st == NULL)
		return FALSE;

	/* get standard FW version */
	std_fw_ver = (guint32)(fu_struct_kinetic_dp_jaguar_footer_get_fw_ver(st) << 8);
	std_fw_ver += fu_struct_kinetic_dp_jaguar_footer_get_fw_rev(st);
	fu_firmware_set_version_raw(FU_FIRMWARE(self), std_fw_ver);

	/* get each block size from FW buffer */
	priv->esm_payload_size = ESM_PAYLOAD_BLOCK_SIZE;
	priv->arm_app_code_size = app_code_block_size;
	priv->app_init_data_size = APP_INIT_DATA_BLOCK_SIZE;
	priv->cmdb_block_size = CMDB_BLOCK_SIZE;
	return TRUE;
}

static gboolean
fu_kinetic_dp_secure_firmware_parse(FuFirmware *firmware,
				    GBytes *fw_bytes,
				    gsize offset,
				    FwupdInstallFlags flags,
				    GError **error)
{
	FuKineticDpSecureFirmware *self = FU_KINETIC_DP_SECURE_FIRMWARE(firmware);
	FuKineticDpSecureFirmwarePrivate *priv = GET_PRIVATE(self);
	const guint8 *buf;
	gsize bufsz;
	guint32 app_fw_payload_size = 0;
	g_autoptr(GBytes) isp_drv_blob = NULL;
	g_autoptr(GBytes) app_fw_blob = NULL;
	g_autoptr(FuFirmware) isp_drv_img = NULL;
	g_autoptr(FuFirmware) app_fw_img = NULL;

	/* parse firmware according to Kinetic's FW image format
	 * FW binary = 4 bytes Header(Little-Endian) + ISP driver + App FW
	 * 4 bytes Header: size of ISP driver */
	buf = g_bytes_get_data(fw_bytes, &bufsz);
	if (!fu_memread_uint32_safe(buf, bufsz, 0, &priv->isp_drv_size, G_LITTLE_ENDIAN, error))
		return FALSE;

	/* app firmware payload size */
	app_fw_payload_size =
	    g_bytes_get_size(fw_bytes) - HEADER_LEN_ISP_DRV_SIZE - priv->isp_drv_size;

	/* add ISP driver as a new image into firmware */
	isp_drv_blob =
	    fu_bytes_new_offset(fw_bytes, HEADER_LEN_ISP_DRV_SIZE, priv->isp_drv_size, error);
	if (isp_drv_blob == NULL)
		return FALSE;
	isp_drv_img = fu_firmware_new_from_bytes(isp_drv_blob);
	fu_firmware_set_idx(isp_drv_img, FU_KINETIC_DP_FIRMWARE_IDX_ISP_DRV);
	fu_firmware_add_image(firmware, isp_drv_img);

	/* add App FW as a new image into firmware */
	app_fw_blob = fu_bytes_new_offset(fw_bytes,
					  HEADER_LEN_ISP_DRV_SIZE + priv->isp_drv_size,
					  app_fw_payload_size,
					  error);
	if (app_fw_blob == NULL)
		return FALSE;
	app_fw_img = fu_firmware_new_from_bytes(app_fw_blob);
	fu_firmware_set_idx(app_fw_img, FU_KINETIC_DP_FIRMWARE_IDX_APP_FW);
	fu_firmware_add_image(firmware, app_fw_img);

	/* figure out which chip App FW it is for */
	if (!fu_kinetic_dp_secure_firmware_parse_chip_id(app_fw_blob,
							 &priv->chip_id,
							 &priv->esm_xip_enabled,
							 error))
		return FALSE;
	if (!fu_kinetic_dp_secure_device_parse_app_fw(self, app_fw_blob, error)) {
		g_prefix_error(error, "failed to parse info from Jaguar or Mustang App firmware: ");
		return FALSE;
	}

	/* success */
	return TRUE;
}

static void
fu_kinetic_dp_secure_firmware_init(FuKineticDpSecureFirmware *self)
{
}

static void
fu_kinetic_dp_secure_firmware_class_init(FuKineticDpSecureFirmwareClass *klass)
{
	FuFirmwareClass *klass_firmware = FU_FIRMWARE_CLASS(klass);
	klass_firmware->parse = fu_kinetic_dp_secure_firmware_parse;
	klass_firmware->export = fu_kinetic_dp_secure_firmware_export;
}
