/*
 * Copyright (C) 2021 Texas Instruments Incorporated
 * Copyright (C) 2022 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+ OR MIT
 */

#pragma once

#include <fwupdplugin.h>

/* registers */
#define TI_TPS6598X_REGISTER_TBT_VID	      0x00 /* ro, 4 bytes -- Intel assigned */
#define TI_TPS6598X_REGISTER_TBT_DID	      0x01 /* ro, 4 bytes -- Intel assigned */
#define TI_TPS6598X_REGISTER_PROTO_VER	      0x02 /* ro, 4 bytes */
#define TI_TPS6598X_REGISTER_MODE	      0x03 /* ro, 4 bytes */
#define TI_TPS6598X_REGISTER_TYPE	      0x04 /* ro, 4 bytes */
#define TI_TPS6598X_REGISTER_UID	      0x05 /* ro, 16 bytes */
#define TI_TPS6598X_REGISTER_OUID	      0x06 /* ro, 8 bytes */
#define TI_TPS6598X_REGISTER_CMD1	      0x08 /* ro, 4CC */
#define TI_TPS6598X_REGISTER_DATA1	      0x09 /* rw, 64 bytes */
#define TI_TPS6598X_REGISTER_VERSION	      0x0F /* rw, 4 bytes */
#define TI_TPS6598X_REGISTER_CMD2	      0x10 /* ro, 4CC */
#define TI_TPS6598X_REGISTER_DATA2	      0x11 /* rw, 64 bytes */
#define TI_TPS6598X_REGISTER_CMD3	      0x1E /* ro, variable */
#define TI_TPS6598X_REGISTER_DATA3	      0x1F /* ro, variable */
#define TI_TPS6598X_REGISTER_OTP_CONFIG	      0x2D /* ro, 12 bytes */
#define TI_TPS6598X_REGISTER_BUILD_IDENTIFIER 0x2E /* ro, 64 bytes */
#define TI_TPS6598X_REGISTER_DEVICE_INFO      0x2F /* ro, 47 bytes */
#define TI_TPS6598X_REGISTER_TX_IDENTITY      0x47 /* rw, 49 bytes */

#define FU_TI_TPS6598X_PD_MAX 2 /* devices */

gboolean
fu_ti_tps6598x_byte_array_is_nonzero(GByteArray *buf);
