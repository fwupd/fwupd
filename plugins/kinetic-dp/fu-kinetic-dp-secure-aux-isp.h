/*
 * Copyright (C) 2021 Jeffrey Lin <jlin@kinet-ic.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#pragma once

#include <glib-object.h>

#include "fu-kinetic-dp-aux-isp.h"
#include "fu-kinetic-dp-device.h"
#include "fu-kinetic-dp-firmware.h"

#define FU_TYPE_KINETIC_DP_SECURE_AUX_ISP (fu_kinetic_dp_secure_aux_isp_get_type())
G_DECLARE_FINAL_TYPE(FuKineticDpSecureAuxIsp,
		     fu_kinetic_dp_secure_aux_isp,
		     FU,
		     KINETIC_DP_SECURE_AUX_ISP,
		     FuKineticDpAuxIsp)

typedef enum {
	/* Status */
	KT_DPCD_CMD_STS_NONE = 0x0,
	KT_DPCD_STS_INVALID_INFO = 0x01,
	KT_DPCD_STS_CRC_FAILURE = 0x02,
	KT_DPCD_STS_INVALID_IMAGE = 0x03,
	KT_DPCD_STS_SECURE_ENABLED = 0x04,
	KT_DPCD_STS_SECURE_DISABLED = 0x05,
	KT_DPCD_STS_SPI_FLASH_FAILURE = 0x06,

	/* Command */
	KT_DPCD_CMD_PREPARE_FOR_ISP_MODE = 0x23,
	KT_DPCD_CMD_ENTER_CODE_LOADING_MODE = 0x24,
	KT_DPCD_CMD_EXECUTE_RAM_CODE = 0x25,
	KT_DPCD_CMD_ENTER_FW_UPDATE_MODE = 0x26,
	KT_DPCD_CMD_CHUNK_DATA_PROCESSED = 0x27,
	KT_DPCD_CMD_INSTALL_IMAGES = 0x28,
	KT_DPCD_CMD_RESET_SYSTEM = 0x29,

	/* Other command */
	KT_DPCD_CMD_ENABLE_AUX_FORWARD = 0x31,
	KT_DPCD_CMD_DISABLE_AUX_FORWARD = 0x32,
	KT_DPCD_CMD_GET_ACTIVE_FLASH_BANK = 0x33,

	/* 0x70 ~ 0x7F are reserved for other usage */
	KT_DPCD_CMD_RESERVED = 0x7F,
} KtSecureAuxIspCmdAndStatus;

typedef enum {
	KT_FW_BIN_FLAG_NONE = 0,
	KT_FW_BIN_FLAG_XIP = 1,
} KtFwBinFlag;

typedef struct {
	guint32 app_id_struct_ver;
	guint8 app_id[4];
	guint32 app_ver_id;
	guint8 fw_major_ver_num;
	guint8 fw_minor_ver_num;
	guint8 fw_rev_num;
	guint8 customer_fw_project_id;
	guint8 customer_fw_major_ver_num;
	guint8 customer_fw_minor_ver_num;
	guint8 chip_rev;
	guint8 is_fpga_enabled;
	guint8 reserved[12];
} KtJaguarAppId;

FuKineticDpSecureAuxIsp *
fu_kinetic_dp_secure_aux_isp_new(void);
gboolean
fu_kinetic_dp_secure_aux_isp_enable_aux_forward(FuKineticDpConnection *connection,
						KtDpDevPort target_port,
						GError **error);
gboolean
fu_kinetic_dp_secure_aux_isp_disable_aux_forward(FuKineticDpConnection *connection, GError **error);
gboolean
fu_kinetic_dp_secure_aux_isp_parse_app_fw(FuKineticDpFirmware *firmware,
					  const guint8 *fw_bin_buf,
					  const guint32 fw_bin_size,
					  const guint16 fw_bin_flag,
					  GError **error);
