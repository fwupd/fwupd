/*
 * Copyright (C) 2020 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#pragma once

#include <fwupdplugin.h>

#define FU_TYPE_UEFI_DBX_DEVICE (fu_uefi_dbx_device_get_type ())
G_DECLARE_FINAL_TYPE (FuUefiDbxDevice, fu_uefi_dbx_device, FU, UEFI_DBX_DEVICE, FuDevice)

FuUefiDbxDevice	*fu_uefi_dbx_device_new			(void);
