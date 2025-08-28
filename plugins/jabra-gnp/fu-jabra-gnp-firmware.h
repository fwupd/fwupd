/*
 * Copyright 2023 GN Audio A/S
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include <fwupdplugin.h>

#include "fu-jabra-gnp-common.h"

#define FU_TYPE_JABRA_GNP_FIRMWARE (fu_jabra_gnp_firmware_get_type())
G_DECLARE_FINAL_TYPE(FuJabraGnpFirmware, fu_jabra_gnp_firmware, FU, JABRA_GNP_FIRMWARE, FuFirmware)
struct _FuJabraGnpFirmware {
	FuFirmware parent_instance;
	guint16 dfu_pid;
	FuJabraGnpVersionData version_data;
};

FuFirmware *
fu_jabra_gnp_firmware_new(void);
guint16
fu_jabra_gnp_firmware_get_dfu_pid(FuJabraGnpFirmware *self);
FuJabraGnpVersionData *
fu_jabra_gnp_firmware_get_version_data(FuJabraGnpFirmware *self);
