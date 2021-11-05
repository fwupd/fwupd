/*
 * Copyright (C) 2021 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#pragma once

#include <fwupdplugin.h>

#define FU_TYPE_MTD_RECOVERY_DEVICE (fu_mtd_recovery_device_get_type())
G_DECLARE_FINAL_TYPE(FuMtdRecoveryDevice, fu_mtd_recovery_device, FU, MTD_RECOVERY_DEVICE, FuDevice)
