/*
 * Copyright (C) 2020 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include "fu-ccgx-dmc-common.h"

const gchar *
fu_ccgx_dmc_update_model_type_to_string(DmcUpdateModel val)
{
	if (val == DMC_UPDATE_MODEL_NONE)
		return "None";
	if (val == DMC_UPDATE_MODEL_DOWNLOAD_TRIGGER)
		return "Download Trigger";
	if (val == DMC_UPDATE_MODEL_PENDING_RESET)
		return "Pending Reset";
	return NULL;
}

const gchar *
fu_ccgx_dmc_devx_device_type_to_string(DmcDevxDeviceType device_type)
{
	if (device_type == DMC_DEVX_DEVICE_TYPE_INVALID)
		return "invalid";
	if (device_type == DMC_DEVX_DEVICE_TYPE_CCG3)
		return "ccg3";
	if (device_type == DMC_DEVX_DEVICE_TYPE_DMC)
		return "dmc";
	if (device_type == DMC_DEVX_DEVICE_TYPE_CCG4)
		return "ccg4";
	if (device_type == DMC_DEVX_DEVICE_TYPE_CCG5)
		return "ccg5";
	if (device_type == DMC_DEVX_DEVICE_TYPE_HX3)
		return "hx3";
	if (device_type == DMC_DEVX_DEVICE_TYPE_HX3_PD)
		return "hx3-pd";
	if (device_type == DMC_DEVX_DEVICE_TYPE_DMC_PD)
		return "dmc-pd";
	if (device_type == DMC_DEVX_DEVICE_TYPE_SPI)
		return "spi";
	return NULL;
}

const gchar *
fu_ccgx_dmc_img_mode_to_string(DmcImgMode img_mode)
{
	if (img_mode == DMC_IMG_MODE_SINGLE_IMG)
		return "single";
	if (img_mode == DMC_IMG_MODE_DUAL_IMG_SYM)
		return "dual-sym";
	if (img_mode == DMC_IMG_MODE_DUAL_IMG_ASYM)
		return "dual-asym";
	if (img_mode == DMC_IMG_MODE_SINGLE_IMG_WITH_RAM_IMG)
		return "single-with-ram-img";
	return NULL;
}

const gchar *
fu_ccgx_dmc_device_status_to_string(DmcDeviceStatus device_status)
{
	if (device_status == DMC_DEVICE_STATUS_IDLE)
		return "idle";
	if (device_status == DMC_DEVICE_STATUS_UPDATE_IN_PROGRESS)
		return "update-in-progress";
	if (device_status == DMC_DEVICE_STATUS_UPDATE_PARTIAL)
		return "update-partial";
	if (device_status == DMC_DEVICE_STATUS_UPDATE_COMPLETE_FULL)
		return "update-complete-full";
	if (device_status == DMC_DEVICE_STATUS_UPDATE_COMPLETE_PARTIAL)
		return "update-complete-partial";
	if (device_status == DMC_DEVICE_STATUS_UPDATE_PHASE_1_COMPLETE)
		return "update-phase1-complete";
	if (device_status == DMC_DEVICE_STATUS_FW_DOWNLOADED_UPDATE_PEND)
		return "fw-downloaded-update-pend";
	if (device_status == DMC_DEVICE_STATUS_FW_DOWNLOADED_PARTIAL_UPDATE_PEND)
		return "fw-downloaded-partial-update-pend";
	if (device_status == DMC_DEVICE_STATUS_PHASE2_UPDATE_IN_PROGRESS)
		return "phase2-update-in-progress";
	if (device_status == DMC_DEVICE_STATUS_PHASE2_UPDATE_PARTIAL)
		return "phase2-update-partial";
	if (device_status == DMC_DEVICE_STATUS_PHASE2_UPDATE_FACTORY_BACKUP)
		return "phase2-update-factory-backup";
	if (device_status == DMC_DEVICE_STATUS_PHASE2_UPDATE_COMPLETE_PARTIAL)
		return "phase2-update-complete-partial";
	if (device_status == DMC_DEVICE_STATUS_PHASE2_UPDATE_COMPLETE_FULL)
		return "phase2-update-complete-full";
	if (device_status == DMC_DEVICE_STATUS_PHASE2_UPDATE_FAIL_INVALID_FWCT)
		return "phase2-update-fail-invalid-fwct";
	if (device_status == DMC_DEVICE_STATUS_PHASE2_UPDATE_FAIL_INVALID_DOCK_IDENTITY)
		return "phase2-update-fail-invalid-dock-identifier";
	if (device_status == DMC_DEVICE_STATUS_PHASE2_UPDATE_FAIL_INVALID_COMPOSITE_VER)
		return "phase2-update-fail-invalid-composite-ver";
	if (device_status == DMC_DEVICE_STATUS_PHASE2_UPDATE_FAIL_AUTHENTICATION_FAILED)
		return "phase2-update-fail-authentication-failed";
	if (device_status == DMC_DEVICE_STATUS_PHASE2_UPDATE_FAIL_INVALID_ALGORITHM)
		return "phase2-update-fail-invalid-algorithm";
	if (device_status == DMC_DEVICE_STATUS_PHASE2_UPDATE_FAIL_SPI_READ_FAILED)
		return "phase2-update-fail-spi-read-failed";
	if (device_status == DMC_DEVICE_STATUS_PHASE2_UPDATE_FAIL_NO_VALID_KEY)
		return "phase2-update-fail-no-valid-key";
	if (device_status == DMC_DEVICE_STATUS_PHASE2_UPDATE_FAIL_NO_VALID_SPI_PACKAGE)
		return "phase2-update-fail-no-valid-spi-package";
	if (device_status == DMC_DEVICE_STATUS_PHASE2_UPDATE_FAIL_RAM_INIT_FAILED)
		return "phase2-update-fail-ram-init-failed";
	if (device_status == DMC_DEVICE_STATUS_PHASE2_UPDATE_FAIL_FACTORY_BACKUP_FAILED)
		return "phase2-update-fail-factory-backup-failed";
	if (device_status == DMC_DEVICE_STATUS_PHASE2_UPDATE_FAIL_NO_VALID_FACTORY_PACKAGE)
		return "phase2-update-fail-no-valid-factory-package";
	if (device_status == DMC_DEVICE_STATUS_UPDATE_FAIL)
		return "update-fail";
	return NULL;
}

const gchar *
fu_ccgx_dmc_img_status_to_string(DmcImgStatus img_status)
{
	if (img_status == DMC_IMG_STATUS_UNKNOWN)
		return "unknown";
	if (img_status == DMC_IMG_STATUS_VALID)
		return "valid";
	if (img_status == DMC_IMG_STATUS_INVALID)
		return "invalid";
	if (img_status == DMC_IMG_STATUS_RECOVERY)
		return "recovery";
	if (img_status == DMC_IMG_STATUS_RECOVERED_FROM_SECONDARY)
		return "recovered-from-secondary";
	if (img_status == DMC_IMG_STATUS_NOT_SUPPORTED)
		return "not-supported";
	return NULL;
}
