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
	guint8 comments[19];		 /* IMG comments */
} FuSunwinonDfuImageInfo;

/**@brief DFU master used function config definition */
typedef struct {
	/* opaque user context passed to callbacks */
	gpointer user_data;
	/* get information about the firmware to be updated */
	gboolean (*dfu_m_get_img_info)(gpointer user_data,
				       FuSunwinonDfuImageInfo *img_info,
				       GError **error);
	/* get data about the firmware to be updated */
	gboolean (*dfu_m_get_img_data)(gpointer user_data,
				       guint32 addr,
				       guint8 *data,
				       guint16 len,
				       GError **error);
	/* send data to peer device */
	gboolean (*dfu_m_send_data)(gpointer user_data, guint8 *data, guint16 len, GError **error);
	/* send event to app */
	void (*dfu_m_event_handler)(gpointer user_data, FuSunwinonDfuEvent event, guint8 progress);
	/* wait for device ready */
	void (*dfu_m_wait)(gpointer user_data, guint32 ms);
	/* get system current time, in ms */
	guint32 (*dfu_m_get_time)(gpointer user_data);
} FuSunwinonDfuCallback;

typedef struct FuDfuMaster FuDfuMaster;

FuDfuMaster *
fu_sunwinon_util_dfu_master_new(const FuSunwinonDfuCallback *dfu_m_func_cfg,
				guint16 once_send_size);

void
fu_sunwinon_util_dfu_master_free(FuDfuMaster *self);
G_DEFINE_AUTOPTR_CLEANUP_FUNC(FuDfuMaster, fu_sunwinon_util_dfu_master_free)

gboolean
fu_sunwinon_util_dfu_master_start(FuDfuMaster *self, GError **error);

void
fu_sunwinon_util_dfu_master_parse_state_reset(FuDfuMaster *self);

gboolean
fu_sunwinon_util_dfu_master_schedule(FuDfuMaster *self, GError **error);

void
fu_sunwinon_util_dfu_master_cmd_parse(FuDfuMaster *self, const guint8 *data, guint16 len);

void
fu_sunwinon_util_dfu_master_send_data_cmpl_process(FuDfuMaster *self);

void
fu_sunwinon_util_dfu_master_fast_dfu_mode_set(FuDfuMaster *self, guint8 setting);

guint8
fu_sunwinon_util_dfu_master_fast_dfu_mode_get(FuDfuMaster *self);

void
fu_sunwinon_util_dfu_master_fast_send_data_cmpl_process(FuDfuMaster *self);

guint32
fu_sunwinon_util_dfu_master_get_program_size(FuDfuMaster *self);

gboolean
fu_sunwinon_util_dfu_master_send_fw_info_get(FuDfuMaster *self, GError **error);

gboolean
fu_sunwinon_util_dfu_master_parse_fw_info(FuDfuMaster *self,
					  FuSunwinonDfuImageInfo *img_info,
					  const guint8 *data,
					  guint16 len,
					  GError **error);

/*
 * Experimental v2 API (stubs): allow parallel integration without
 * affecting existing call sites. Implementations are provided in the
 * corresponding .c file and currently act as no-op placeholders.
 */

typedef struct FuSwDfuMaster FuSwDfuMaster;

FuSwDfuMaster *
fu_sunwinon_util_dfu_master_2_new(const guint8 *fw, gsize fw_sz, FuDevice *device);

void
fu_sunwinon_util_dfu_master_2_free(FuSwDfuMaster *self);
G_DEFINE_AUTOPTR_CLEANUP_FUNC(FuSwDfuMaster, fu_sunwinon_util_dfu_master_2_free)

gboolean
fu_sunwinon_util_dfu_master_2_fetch_fw_version(FuSwDfuMaster *self,
					       FuSunwinonDfuImageInfo *out,
					       GError **error);

gboolean
fu_sunwinon_util_dfu_master_2_start(FuSwDfuMaster *self,
				    FuProgress *progress,
				    guint8 mode_setting,
				    GError **error);

gboolean
fu_sunwinon_util_dfu_master_2_write_firmware(FuSwDfuMaster *self,
					     FuProgress *progress,
					     GError **error);
// Removed fast_dfu_mode_set_2 as per requirement to merge mode set into start.
