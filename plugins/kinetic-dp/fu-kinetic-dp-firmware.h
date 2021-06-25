/*
 * Copyright (C) 2021 Richard Hughes <richard@hughsie.com>
 * Copyright (C) 2021 Jeffrey Lin <jlin@kinet-ic.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#pragma once

#include <fwupdplugin.h>

#define FU_TYPE_KINETIC_DP_FIRMWARE (fu_kinetic_dp_firmware_get_type())
G_DECLARE_FINAL_TYPE(FuKineticDpFirmware,
		     fu_kinetic_dp_firmware,
		     FU,
		     KINETIC_DP_FIRMWARE,
		     FuFirmware)

typedef enum {
	FU_KT_FW_IMG_IDX_ISP_DRV = 0,
	FU_KT_FW_IMG_IDX_APP_FW = 1,
} FuKineticFwImgIdx;

FuFirmware *
fu_kinetic_dp_firmware_new(void);

void
fu_kinetic_dp_firmware_set_isp_drv_size(FuKineticDpFirmware *self, guint32 isp_drv_size);
guint32
fu_kinetic_dp_firmware_get_isp_drv_size(FuKineticDpFirmware *self);
void
fu_kinetic_dp_firmware_set_esm_payload_size(FuKineticDpFirmware *self, guint32 esm_payload_size);
guint32
fu_kinetic_dp_firmware_get_esm_payload_size(FuKineticDpFirmware *self);
void
fu_kinetic_dp_firmware_set_arm_app_code_size(FuKineticDpFirmware *self, guint32 arm_app_code_size);
guint32
fu_kinetic_dp_firmware_get_arm_app_code_size(FuKineticDpFirmware *self);
void
fu_kinetic_dp_firmware_set_app_init_data_size(FuKineticDpFirmware *self,
					      guint16 app_init_data_size);
guint16
fu_kinetic_dp_firmware_get_app_init_data_size(FuKineticDpFirmware *self);
void
fu_kinetic_dp_firmware_set_cmdb_block_size(FuKineticDpFirmware *self, guint16 cmdb_block_size);
guint16
fu_kinetic_dp_firmware_get_cmdb_block_size(FuKineticDpFirmware *self);
void
fu_kinetic_dp_firmware_set_is_fw_esm_xip_enabled(FuKineticDpFirmware *self,
						 gboolean is_fw_esm_xip_enabled);
gboolean
fu_kinetic_dp_firmware_get_is_fw_esm_xip_enabled(FuKineticDpFirmware *self);
void
fu_kinetic_dp_firmware_set_std_fw_ver(FuKineticDpFirmware *self, guint32 std_fw_ver);
guint32
fu_kinetic_dp_firmware_get_std_fw_ver(FuKineticDpFirmware *self);
void
fu_kinetic_dp_firmware_set_customer_fw_ver(FuKineticDpFirmware *self, guint32 customer_fw_ver);
guint32
fu_kinetic_dp_firmware_get_customer_fw_ver(FuKineticDpFirmware *self);
void
fu_kinetic_dp_firmware_set_customer_project_id(FuKineticDpFirmware *self,
					       guint32 customer_project_id);
guint8
fu_kinetic_dp_firmware_get_customer_project_id(FuKineticDpFirmware *self);
guint32
fu_kinetic_dp_firmware_get_valid_payload_size(const guint8 *payload_data, const guint32 data_size);
