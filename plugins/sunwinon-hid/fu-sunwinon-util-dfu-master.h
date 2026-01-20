/*
 * SPDX-License-Identifier: LGPL-2.1-or-later OR BSD-3-Clause
 */

#pragma once

#include <fwupdplugin.h>

#include "fu-sunwinon-hid-struct.h"

/* DFU config */
#define FU_SUNWINON_DFU_CONFIG_PERIPHERAL_FLASH_START_ADDR 0x00200000U
#define FU_SUNWINON_DFU_CONFIG_ONCE_PROGRAM_LEN		   464U
#define FU_SUNWINON_DFU_CONFIG_SEND_SIZE_MAX		   517U
#define FU_SUNWINON_DFU_CONFIG_ACK_WAIT_TIMEOUT		   4000U
#define FU_SUNWINON_DFU_CONFIG_PERIPHERAL_RESET_TIME	   2000U

/* Frame size limits */
#define FU_SUNWINON_DFU_FRAME_MAX_TX (FU_SUNWINON_DFU_CONFIG_ONCE_PROGRAM_LEN + 15)
#define FU_SUNWINON_DFU_FRAME_MAX_RX 64

typedef struct {
	guint32 bin_size;
	guint32 check_sum;
	guint32 load_addr;
	guint32 run_addr;
	guint32 xqspi_xip_cmd;
	guint32 xqspi_speed : 4;
	guint32 code_copy_mode : 1;
	guint32 system_clk : 3;
	guint32 check_image : 1;
	guint32 boot_delay : 1;
	guint32 signature_algorithm : 2;
	guint32 reserved : 20;
} FuSunwinonDfuBootInfo;

typedef struct {
	guint16 pattern;		 /* IMG info pattern */
	guint16 version;		 /* IMG version */
	FuSunwinonDfuBootInfo boot_info; /* IMG boot info */
	guint8 comments[11];		 /* IMG comments */
} FuSunwinonDfuImageInfo;

typedef struct FuSwDfuMaster FuSwDfuMaster;

FuSwDfuMaster *
fu_sunwinon_util_dfu_master_new(const guint8 *fw, gsize fw_sz, FuDevice *device, GError **error);

void
fu_sunwinon_util_dfu_master_free(FuSwDfuMaster *self);
G_DEFINE_AUTOPTR_CLEANUP_FUNC(FuSwDfuMaster, fu_sunwinon_util_dfu_master_free)

gboolean
fu_sunwinon_util_dfu_master_fetch_fw_version(FuSwDfuMaster *self,
					     FuSunwinonDfuImageInfo *image_info,
					     GError **error);

gboolean
fu_sunwinon_util_dfu_master_start(FuSwDfuMaster *self,
				  FuProgress *progress,
				  guint8 mode_setting,
				  GError **error);

gboolean
fu_sunwinon_util_dfu_master_write_firmware(FuSwDfuMaster *self,
					   FuProgress *progress,
					   FuSunwinonFastDfuMode fast_mode,
					   FuSunwinonDfuUpgradeMode copy_mode,
					   GError **error);
