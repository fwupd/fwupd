/*
 * Copyright (C) 2022 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#pragma once

#include <glib-object.h>

#define FU_WISTRON_DOCK_CMD_ICP_ENTER	     0x81
#define FU_WISTRON_DOCK_CMD_ICP_EXIT	     0x82
#define FU_WISTRON_DOCK_CMD_ICP_ADDRESS	     0x84
#define FU_WISTRON_DOCK_CMD_ICP_READBLOCK    0x85
#define FU_WISTRON_DOCK_CMD_ICP_WRITEBLOCK   0x86
#define FU_WISTRON_DOCK_CMD_ICP_MCUID	     0x87
#define FU_WISTRON_DOCK_CMD_ICP_BBINFO	     0x88 /* bb code information */
#define FU_WISTRON_DOCK_CMD_ICP_USERINFO     0x89 /* user code information */
#define FU_WISTRON_DOCK_CMD_ICP_DONE	     0x5A
#define FU_WISTRON_DOCK_CMD_ICP_ERROR	     0xFF
#define FU_WISTRON_DOCK_CMD_ICP_EXIT_WDRESET 0x01 /* exit ICP with watch dog reset */

#define FU_WISTRON_DOCK_CMD_DFU_ENTER		0x91
#define FU_WISTRON_DOCK_CMD_DFU_EXIT		0x92
#define FU_WISTRON_DOCK_CMD_DFU_ADDRESS		0x93
#define FU_WISTRON_DOCK_CMD_DFU_READIMG_BLOCK	0x94
#define FU_WISTRON_DOCK_CMD_DFU_WRITEIMG_BLOCK	0x95
#define FU_WISTRON_DOCK_CMD_DFU_VERIFY		0x96
#define FU_WISTRON_DOCK_CMD_DFU_COMPOSITE_VER	0x97
#define FU_WISTRON_DOCK_CMD_DFU_WRITE_WDFL_SIG	0x98
#define FU_WISTRON_DOCK_CMD_DFU_WRITE_WDFL_DATA 0x99
#define FU_WISTRON_DOCK_CMD_DFU_VERIFY_WDFL	0x9A
#define FU_WISTRON_DOCK_CMD_DFU_SERINAL_NUMBER	0x9B
#define FU_WISTRON_DOCK_CMD_DFU_DONE		0x5A
#define FU_WISTRON_DOCK_CMD_DFU_ERROR		0xFF

#define FU_WISTRON_DOCK_COMPONENT_IDX_MCU   0x0
#define FU_WISTRON_DOCK_COMPONENT_IDX_PD    0x1
#define FU_WISTRON_DOCK_COMPONENT_IDX_AUDIO 0x2
#define FU_WISTRON_DOCK_COMPONENT_IDX_USB   0x3
#define FU_WISTRON_DOCK_COMPONENT_IDX_MST   0x4
#define FU_WISTRON_DOCK_COMPONENT_IDX_SPI   0xA
#define FU_WISTRON_DOCK_COMPONENT_IDX_DOCK  0xF

#define FU_WISTRON_DOCK_UPDATE_PHASE_DOWNLOAD 0x1
#define FU_WISTRON_DOCK_UPDATE_PHASE_DEPLOY   0x2

#define FU_WISTRON_DOCK_STATUS_CODE_ENTER    0x01
#define FU_WISTRON_DOCK_STATUS_CODE_PREPARE  0x02
#define FU_WISTRON_DOCK_STATUS_CODE_UPDATING 0x03
#define FU_WISTRON_DOCK_STATUS_CODE_COMPLETE 0x04 /* unplug cable to trigger update */

#define FU_WISTRON_DOCK_WDIT_SIZE   512	   /* bytes */
#define FU_WISTRON_DOCK_WDIT_TAG_ID 0x4954 /* 'IT' */

#define FU_WISTRON_DOCK_WDFL_SIG_SIZE  256  /* bytes */
#define FU_WISTRON_DOCK_WDFL_DATA_SIZE 1328 /* bytes */

const gchar *
fu_wistron_dock_component_idx_to_string(guint8 component_idx);
const gchar *
fu_wistron_dock_update_phase_to_string(guint8 update_phase);
const gchar *
fu_wistron_dock_status_code_to_string(guint8 status_code);
