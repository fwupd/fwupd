/*
 * Copyright (C) 2022 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include "fu-wistron-dock-common.h"

const gchar *
fu_wistron_dock_update_phase_to_string(guint8 update_phase)
{
	if (update_phase == FU_WISTRON_DOCK_UPDATE_PHASE_DOWNLOAD)
		return "download";
	if (update_phase == FU_WISTRON_DOCK_UPDATE_PHASE_DEPLOY)
		return "deploy";
	return NULL;
}

const gchar *
fu_wistron_dock_component_idx_to_string(guint8 component_idx)
{
	if (component_idx == FU_WISTRON_DOCK_COMPONENT_IDX_MCU)
		return "mcu";
	if (component_idx == FU_WISTRON_DOCK_COMPONENT_IDX_PD)
		return "pd";
	if (component_idx == FU_WISTRON_DOCK_COMPONENT_IDX_AUDIO)
		return "audio";
	if (component_idx == FU_WISTRON_DOCK_COMPONENT_IDX_USB)
		return "usb";
	if (component_idx == FU_WISTRON_DOCK_COMPONENT_IDX_MST)
		return "mst";
	if (component_idx == FU_WISTRON_DOCK_COMPONENT_IDX_SPI)
		return "spi";
	if (component_idx == FU_WISTRON_DOCK_COMPONENT_IDX_DOCK)
		return "dock";
	return NULL;
}

const gchar *
fu_wistron_dock_status_code_to_string(guint8 status_code)
{
	if (status_code == FU_WISTRON_DOCK_STATUS_CODE_ENTER)
		return "enter";
	if (status_code == FU_WISTRON_DOCK_STATUS_CODE_PREPARE)
		return "prepare";
	if (status_code == FU_WISTRON_DOCK_STATUS_CODE_UPDATING)
		return "updating";
	if (status_code == FU_WISTRON_DOCK_STATUS_CODE_COMPLETE)
		return "complete";
	return NULL;
}
