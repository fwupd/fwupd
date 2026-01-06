/*
 * SPDX-License-Identifier: LGPL-2.1-or-later
 *
 * Note: This file is derived from a BSD-licensed DFU master implementation
 * provided by GOODIX and has been relicensed to LGPL-2.1-or-later for fwupd.
 * See plugins/sunwinon-hid/GOODIX-BSD-LICENSE for the original terms.
 */
#pragma once

#include "fu-sunwinon-hid-struct.h"
#include "glib.h"

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
	guint16 pattern;		 /* IMG info pattern. */
	guint16 version;		 /* IMG version. */
	FuSunwinonDfuBootInfo boot_info; /* IMG boot info. */
	guint8 comments[12];		 /* IMG comments. */
} FuSunwinonDfuImageInfo;

/**@brief DFU master used function config definition. */
typedef struct {
	/* Opaque user context passed to callbacks. */
	gpointer user_data;
	/* Get information about the firmware to be updated. */
	gboolean (*dfu_m_get_img_info)(gpointer user_data,
				       FuSunwinonDfuImageInfo *img_info,
				       GError **error);
	/* Get data about the firmware to be updated. */
	gboolean (*dfu_m_get_img_data)(gpointer user_data,
				       guint32 addr,
				       guint8 *data,
				       guint16 len,
				       GError **error);
	/* Send data to peer device. */
	gboolean (*dfu_m_send_data)(gpointer user_data, guint8 *data, guint16 len, GError **error);
	/* Send event to app. */
	void (*dfu_m_event_handler)(gpointer user_data, FuSunwinonDfuEvent event, guint8 progress);
	/* Get system current time, in ms. */
	guint32 (*dfu_m_get_time)(gpointer user_data);
} FuSunwinonDfuCallback;

typedef struct FuDfuMaster FuDfuMaster;

FuDfuMaster *
dfu_m_new(const FuSunwinonDfuCallback *dfu_m_func_cfg, guint16 once_send_size);

void
dfu_m_free(FuDfuMaster *self);
G_DEFINE_AUTOPTR_CLEANUP_FUNC(FuDfuMaster, dfu_m_free)

gboolean
dfu_m_start(FuDfuMaster *self, GError **error);

void
dfu_m_parse_state_reset(FuDfuMaster *self);

gboolean
dfu_m_schedule(FuDfuMaster *self, GError **error);

void
dfu_m_cmd_parse(FuDfuMaster *self, const guint8 *data, guint16 len);

void
dfu_m_send_data_cmpl_process(FuDfuMaster *self);

void
dfu_m_fast_dfu_mode_set(FuDfuMaster *self, guint8 setting);

guint8
dfu_m_fast_dfu_mode_get(FuDfuMaster *self);

void
dfu_m_fast_send_data_cmpl_process(FuDfuMaster *self);

guint32
dfu_m_get_program_size(FuDfuMaster *self);

gboolean
dfu_m_send_fw_info_get(FuDfuMaster *self, GError **error);

gboolean
dfu_m_parse_fw_info(FuDfuMaster *self,
		    FuSunwinonDfuImageInfo *img_info,
		    const guint8 *data,
		    guint16 len,
		    GError **error);
