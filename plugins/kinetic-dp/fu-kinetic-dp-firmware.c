/*
 * Copyright (C) 2021 Richard Hughes <richard@hughsie.com>
 * Copyright (C) 2021 Jeffrey Lin <jlin@kinet-ic.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "fu-kinetic-dp-firmware.h"

#include "fu-kinetic-dp-aux-isp.h"
#include "fu-kinetic-dp-connection.h"
#include "fu-kinetic-dp-secure-aux-isp.h"

struct _FuKineticDpFirmware {
	FuFirmwareClass parent_instance;
};

typedef struct {
	KtChipId chip_id;
	guint32 isp_drv_size;
	guint32 esm_payload_size;
	guint32 arm_app_code_size;
	guint16 app_init_data_size;
	guint16 cmdb_block_size;
	gboolean is_fw_esm_xip_enabled;
	guint16 fw_bin_flag;
	/* FW info embedded in FW file */
	guint32 std_fw_ver;
	guint32 customer_fw_ver;
	guint8 customer_project_id;
} FuKineticDpFirmwarePrivate;

G_DEFINE_TYPE_WITH_PRIVATE(FuKineticDpFirmware, fu_kinetic_dp_firmware, FU_TYPE_FIRMWARE)
#define GET_PRIVATE(o) (fu_kinetic_dp_firmware_get_instance_private(o))

#define HEADER_LEN_ISP_DRV_SIZE 4
#define APP_ID_STR_LEN		4

typedef struct {
	KtChipId chip_id;
	guint32 app_id_offset;
	guint8 app_id_str[APP_ID_STR_LEN];
	guint16 fw_bin_flag;
} KtDpFwAppIdFlag;

/* Application signature/Identifier table */
static const KtDpFwAppIdFlag kt_dp_app_sign_id_table[] = {
    /* Chip_ID		App ID Offset	App ID			FW Flag
     */
    {KT_CHIP_JAGUAR_5000,
     0x0FFFE4UL,
     {'J', 'A', 'G', 'R'},
     KT_FW_BIN_FLAG_NONE}, /* Jaguar 1024KB			*/
    {KT_CHIP_JAGUAR_5000,
     0x0A7036UL,
     {'J', 'A', 'G', 'R'},
     KT_FW_BIN_FLAG_NONE}, /* Jaguar 670KB, for ANZU		*/
    {KT_CHIP_JAGUAR_5000,
     0x0FFFE4UL,
     {'J', 'A', 'G', 'X'},
     KT_FW_BIN_FLAG_XIP}, /* Jaguar 1024KB (App 640KB)		*/
    {KT_CHIP_JAGUAR_5000,
     0x0E7036UL,
     {'J', 'A', 'G', 'X'},
     KT_FW_BIN_FLAG_XIP}, /* Jaguar 670KB, for ANZU (App 640KB)	*/
    {KT_CHIP_MUSTANG_5200,
     0x0FFFE4UL,
     {'M', 'S', 'T', 'G'},
     KT_FW_BIN_FLAG_NONE}, /* Mustang 1024KB			*/
    {KT_CHIP_MUSTANG_5200,
     0x0A7036UL,
     {'M', 'S', 'T', 'G'},
     KT_FW_BIN_FLAG_NONE}, /* Mustang 670KB, for ANZU		*/
    {KT_CHIP_MUSTANG_5200,
     0x0FFFE4UL,
     {'M', 'S', 'T', 'X'},
     KT_FW_BIN_FLAG_XIP}, /* Mustang 1024KB (App 640KB)		*/
    {KT_CHIP_MUSTANG_5200,
     0x0E7036UL,
     {'M', 'S', 'T', 'X'},
     KT_FW_BIN_FLAG_XIP}, /* Mustang 670KB, for ANZU (App 640KB)	*/
};

static gboolean
fu_kinetic_dp_firmware_get_chip_id_from_fw_buf(const guint8 *fw_bin_buf,
					       const guint32 fw_bin_size,
					       KtChipId *chip_id,
					       guint16 *fw_bin_flag)
{
	guint32 num = G_N_ELEMENTS(kt_dp_app_sign_id_table);
	for (guint32 i = 0; i < num; i++) {
		guint32 app_id_offset = kt_dp_app_sign_id_table[i].app_id_offset;

		if ((app_id_offset + APP_ID_STR_LEN) < fw_bin_size) {
			if (memcmp(&fw_bin_buf[app_id_offset],
				   kt_dp_app_sign_id_table[i].app_id_str,
				   APP_ID_STR_LEN) == 0) {
				/* found corresponding app ID */
				*chip_id = kt_dp_app_sign_id_table[i].chip_id;
				*fw_bin_flag = kt_dp_app_sign_id_table[i].fw_bin_flag;
				return TRUE;
			}
		}
	}

	return FALSE;
}

void
fu_kinetic_dp_firmware_set_isp_drv_size(FuKineticDpFirmware *self, guint32 isp_drv_size)
{
	FuKineticDpFirmwarePrivate *priv = GET_PRIVATE(self);
	g_return_if_fail(FU_IS_KINETIC_DP_FIRMWARE(self));
	priv->isp_drv_size = isp_drv_size;
}

guint32
fu_kinetic_dp_firmware_get_isp_drv_size(FuKineticDpFirmware *self)
{
	FuKineticDpFirmwarePrivate *priv = GET_PRIVATE(self);
	g_return_val_if_fail(FU_KINETIC_DP_FIRMWARE(self), 0);
	return priv->isp_drv_size;
}

void
fu_kinetic_dp_firmware_set_esm_payload_size(FuKineticDpFirmware *self, guint32 esm_payload_size)
{
	FuKineticDpFirmwarePrivate *priv = GET_PRIVATE(self);
	g_return_if_fail(FU_IS_KINETIC_DP_FIRMWARE(self));
	priv->esm_payload_size = esm_payload_size;
}

guint32
fu_kinetic_dp_firmware_get_esm_payload_size(FuKineticDpFirmware *self)
{
	FuKineticDpFirmwarePrivate *priv = GET_PRIVATE(self);
	g_return_val_if_fail(FU_KINETIC_DP_FIRMWARE(self), 0);
	return priv->esm_payload_size;
}

void
fu_kinetic_dp_firmware_set_arm_app_code_size(FuKineticDpFirmware *self, guint32 arm_app_code_size)
{
	FuKineticDpFirmwarePrivate *priv = GET_PRIVATE(self);
	g_return_if_fail(FU_IS_KINETIC_DP_FIRMWARE(self));
	priv->arm_app_code_size = arm_app_code_size;
}

guint32
fu_kinetic_dp_firmware_get_arm_app_code_size(FuKineticDpFirmware *self)
{
	FuKineticDpFirmwarePrivate *priv = GET_PRIVATE(self);
	g_return_val_if_fail(FU_KINETIC_DP_FIRMWARE(self), 0);
	return priv->arm_app_code_size;
}

void
fu_kinetic_dp_firmware_set_app_init_data_size(FuKineticDpFirmware *self, guint16 app_init_data_size)
{
	FuKineticDpFirmwarePrivate *priv = GET_PRIVATE(self);
	g_return_if_fail(FU_IS_KINETIC_DP_FIRMWARE(self));
	priv->app_init_data_size = app_init_data_size;
}

guint16
fu_kinetic_dp_firmware_get_app_init_data_size(FuKineticDpFirmware *self)
{
	FuKineticDpFirmwarePrivate *priv = GET_PRIVATE(self);
	g_return_val_if_fail(FU_KINETIC_DP_FIRMWARE(self), 0);
	return priv->app_init_data_size;
}

void
fu_kinetic_dp_firmware_set_cmdb_block_size(FuKineticDpFirmware *self, guint16 cmdb_block_size)
{
	FuKineticDpFirmwarePrivate *priv = GET_PRIVATE(self);
	g_return_if_fail(FU_IS_KINETIC_DP_FIRMWARE(self));
	priv->cmdb_block_size = cmdb_block_size;
}

guint16
fu_kinetic_dp_firmware_get_cmdb_block_size(FuKineticDpFirmware *self)
{
	FuKineticDpFirmwarePrivate *priv = GET_PRIVATE(self);
	g_return_val_if_fail(FU_KINETIC_DP_FIRMWARE(self), 0);
	return priv->cmdb_block_size;
}

void
fu_kinetic_dp_firmware_set_is_fw_esm_xip_enabled(FuKineticDpFirmware *self,
						 gboolean is_fw_esm_xip_enabled)
{
	FuKineticDpFirmwarePrivate *priv = GET_PRIVATE(self);
	g_return_if_fail(FU_IS_KINETIC_DP_FIRMWARE(self));
	priv->is_fw_esm_xip_enabled = is_fw_esm_xip_enabled;
}

gboolean
fu_kinetic_dp_firmware_get_is_fw_esm_xip_enabled(FuKineticDpFirmware *self)
{
	FuKineticDpFirmwarePrivate *priv = GET_PRIVATE(self);
	g_return_val_if_fail(FU_KINETIC_DP_FIRMWARE(self), FALSE);
	return priv->is_fw_esm_xip_enabled;
}

void
fu_kinetic_dp_firmware_set_std_fw_ver(FuKineticDpFirmware *self, guint32 std_fw_ver)
{
	FuKineticDpFirmwarePrivate *priv = GET_PRIVATE(self);
	g_return_if_fail(FU_IS_KINETIC_DP_FIRMWARE(self));
	priv->std_fw_ver = std_fw_ver;
}

guint32
fu_kinetic_dp_firmware_get_std_fw_ver(FuKineticDpFirmware *self)
{
	FuKineticDpFirmwarePrivate *priv = GET_PRIVATE(self);
	g_return_val_if_fail(FU_KINETIC_DP_FIRMWARE(self), 0);
	return priv->std_fw_ver;
}

void
fu_kinetic_dp_firmware_set_customer_fw_ver(FuKineticDpFirmware *self, guint32 customer_fw_ver)
{
	FuKineticDpFirmwarePrivate *priv = GET_PRIVATE(self);
	g_return_if_fail(FU_IS_KINETIC_DP_FIRMWARE(self));
	priv->customer_fw_ver = customer_fw_ver;
}

guint32
fu_kinetic_dp_firmware_get_customer_fw_ver(FuKineticDpFirmware *self)
{
	FuKineticDpFirmwarePrivate *priv = GET_PRIVATE(self);
	g_return_val_if_fail(FU_KINETIC_DP_FIRMWARE(self), 0);
	return priv->customer_fw_ver;
}

void
fu_kinetic_dp_firmware_set_customer_project_id(FuKineticDpFirmware *self,
					       guint32 customer_project_id)
{
	FuKineticDpFirmwarePrivate *priv = GET_PRIVATE(self);
	g_return_if_fail(FU_IS_KINETIC_DP_FIRMWARE(self));
	priv->customer_project_id = customer_project_id;
}

guint8
fu_kinetic_dp_firmware_get_customer_project_id(FuKineticDpFirmware *self)
{
	FuKineticDpFirmwarePrivate *priv = GET_PRIVATE(self);
	g_return_val_if_fail(FU_KINETIC_DP_FIRMWARE(self), 0);
	return priv->customer_project_id;
}

guint32
fu_kinetic_dp_firmware_get_valid_payload_size(const guint8 *payload_data, const guint32 data_size)
{
	guint32 i = 0;

	payload_data += data_size - 1; /* start searching from the end of payload */
	while ((*(payload_data - i) == 0xFF) && (i < data_size)) {
		i++;
	}

	return (data_size - i);
}

static gboolean
fu_kinetic_dp_firmware_parse(FuFirmware *self,
			     GBytes *fw_bytes,
			     guint64 addr_start,
			     guint64 addr_end,
			     FwupdInstallFlags flags,
			     GError **error)
{
	FuKineticDpFirmware *firmware = FU_KINETIC_DP_FIRMWARE(self);
	FuKineticDpFirmwarePrivate *priv = GET_PRIVATE(firmware);
	const guint8 *buf;
	gsize bufsz;
	guint32 app_fw_payload_size = 0;
	g_autoptr(GBytes) isp_drv_payload = NULL;
	g_autoptr(GBytes) app_fw_payload = NULL;
	g_autoptr(FuFirmware) isp_drv_img = NULL;
	g_autoptr(FuFirmware) app_fw_img = NULL;

	/* parse firmware according to Kinetic's FW image format
	 * FW binary = 4 bytes Header(Little-Endian) + ISP driver + App FW
	 * 4 bytes: size of ISP driver
	 */
	buf = g_bytes_get_data(fw_bytes, &bufsz);
	if (!fu_common_read_uint32_safe(buf,
					bufsz,
					0,
					&priv->isp_drv_size,
					G_LITTLE_ENDIAN,
					error)) {
		return FALSE;
	}
	g_debug("ISP driver payload size: %u bytes", priv->isp_drv_size);

	app_fw_payload_size =
	    g_bytes_get_size(fw_bytes) - HEADER_LEN_ISP_DRV_SIZE - priv->isp_drv_size;
	g_debug("app FW payload size: %u bytes", app_fw_payload_size);

	/* add ISP driver as a new image into firmware */
	isp_drv_payload =
	    g_bytes_new_from_bytes(fw_bytes, HEADER_LEN_ISP_DRV_SIZE, priv->isp_drv_size);
	isp_drv_img = fu_firmware_new_from_bytes(isp_drv_payload);
	fu_firmware_set_idx(isp_drv_img, FU_KT_FW_IMG_IDX_ISP_DRV);

	fu_firmware_add_image(self, isp_drv_img);

	/* add App FW as a new image into firmware */
	app_fw_payload = g_bytes_new_from_bytes(fw_bytes,
						HEADER_LEN_ISP_DRV_SIZE + priv->isp_drv_size,
						app_fw_payload_size);
	buf = g_bytes_get_data(app_fw_payload, &bufsz);
	if (!fu_kinetic_dp_firmware_get_chip_id_from_fw_buf(buf,
							    bufsz,
							    &priv->chip_id,
							    &priv->fw_bin_flag)) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INTERNAL,
				    "no valid chip ID is found in the firmware");
		return FALSE;
	}

	if (priv->chip_id == KT_CHIP_JAGUAR_5000 || priv->chip_id == KT_CHIP_MUSTANG_5200) {
		if (!fu_kinetic_dp_secure_aux_isp_parse_app_fw(firmware,
							       buf,
							       bufsz,
							       priv->fw_bin_flag,
							       error)) {
			g_prefix_error(error, "failed to parse FW info from firmware file: ");
			return FALSE;
		}
	} else {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_NOT_SUPPORTED,
				    "unsupported firmware");
		return FALSE;
	}

	app_fw_img = fu_firmware_new_from_bytes(app_fw_payload);
	fu_firmware_set_idx(app_fw_img, FU_KT_FW_IMG_IDX_APP_FW);
	fu_firmware_add_image(self, app_fw_img);
	return TRUE;
}

static void
fu_kinetic_dp_firmware_init(FuKineticDpFirmware *self)
{
	FuKineticDpFirmwarePrivate *priv = GET_PRIVATE(self);

	priv->chip_id = KT_CHIP_NONE;
	priv->isp_drv_size = 0;
	priv->esm_payload_size = 0;
	priv->arm_app_code_size = 0;
	priv->app_init_data_size = 0;
	priv->cmdb_block_size = 0;
	priv->is_fw_esm_xip_enabled = FALSE;
	priv->fw_bin_flag = 0;
}

static void
fu_kinetic_dp_firmware_class_init(FuKineticDpFirmwareClass *klass)
{
	FuFirmwareClass *klass_firmware = FU_FIRMWARE_CLASS(klass);
	klass_firmware->parse = fu_kinetic_dp_firmware_parse;
}

FuFirmware *
fu_kinetic_dp_firmware_new(void)
{
	return FU_FIRMWARE(g_object_new(FU_TYPE_KINETIC_DP_FIRMWARE, NULL));
}
