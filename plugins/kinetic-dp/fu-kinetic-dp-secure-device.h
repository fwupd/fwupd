/*
 * Copyright 2021 Jeffrey Lin <jlin@kinet-ic.com>
 * Copyright 2022 Hai Su <hsu@kinet-ic.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include "fu-kinetic-dp-device.h"

#define FU_TYPE_KINETIC_DP_SECURE_DEVICE (fu_kinetic_dp_secure_device_get_type())
G_DECLARE_FINAL_TYPE(FuKineticDpSecureDevice,
		     fu_kinetic_dp_secure_device,
		     FU,
		     KINETIC_DP_SECURE_DEVICE,
		     FuKineticDpDevice)

/* Flash Memory Map */
#define STD_FW_PAYLOAD_SIZE FU_MiB(1)
#define CUSTOMER_PROJ_ID_OFFSET                                                                    \
	(STD_FW_PAYLOAD_SIZE - FU_STRUCT_KINETIC_DP_JAGUAR_FOOTER_SIZE + 15) /* 0xFFFEF */
#define CUSTOMER_FW_VER_OFFSET                                                                     \
	(STD_FW_PAYLOAD_SIZE - FU_STRUCT_KINETIC_DP_JAGUAR_FOOTER_SIZE + 16) /* 0xFFFF0 */
#define CUSTOMER_FW_VER_SIZE 2

#define FW_CERTIFICATE_SIZE	    FU_KiB(1)
#define FW_RSA_SIGNATURE_SIZE	    256
#define FW_RSA_SIGNATURE_BLOCK_SIZE FU_KiB(1)
#define ESM_PAYLOAD_BLOCK_SIZE	    FU_KiB(256)
#define APP_CODE_NORMAL_BLOCK_SIZE  FU_KiB(384)
#define APP_CODE_EXTEND_BLOCK_SIZE  FU_KiB(640)
#define APP_INIT_DATA_BLOCK_SIZE    FU_KiB(24)
#define CMDB_BLOCK_SIZE		    FU_KiB(4)

#define SPI_ESM_CERTIFICATE_START   0
#define SPI_APP_CERTIFICATE_START   (SPI_ESM_CERTIFICATE_START + FW_CERTIFICATE_SIZE) /*0x00400*/
#define SPI_ESM_RSA_SIGNATURE_START (SPI_APP_CERTIFICATE_START + FW_CERTIFICATE_SIZE) /*0x00800*/
#define SPI_APP_RSA_SIGNATURE_START                                                                \
	(SPI_ESM_RSA_SIGNATURE_START + FW_RSA_SIGNATURE_BLOCK_SIZE) /*0x00C00*/
#define SPI_ESM_PAYLOAD_START                                                                      \
	(SPI_APP_RSA_SIGNATURE_START + FW_RSA_SIGNATURE_BLOCK_SIZE)	       /*0x01000*/
#define SPI_APP_PAYLOAD_START (SPI_ESM_PAYLOAD_START + ESM_PAYLOAD_BLOCK_SIZE) /*0x41000*/
#define SPI_APP_NORMAL_INIT_DATA_START                                                             \
	(SPI_APP_PAYLOAD_START + APP_CODE_NORMAL_BLOCK_SIZE) /*0xA1000*/
#define SPI_APP_EXTEND_INIT_DATA_START                                                             \
	(SPI_APP_PAYLOAD_START + APP_CODE_EXTEND_BLOCK_SIZE) /*0xE1000*/
#define SPI_CMDB_BLOCK_START  0xFE000UL
#define SPI_APP_ID_DATA_START (STD_FW_PAYLOAD_SIZE - FU_STRUCT_KINETIC_DP_JAGUAR_FOOTER_SIZE)
