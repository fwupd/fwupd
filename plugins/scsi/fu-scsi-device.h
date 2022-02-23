/*
 * Copyright (C) 2022 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#pragma once

#include <fwupdplugin.h>

#define FU_TYPE_SCSI_DEVICE (fu_scsi_device_get_type())
G_DECLARE_FINAL_TYPE(FuScsiDevice, fu_scsi_device, FU, SCSI_DEVICE, FuUdevDevice)
