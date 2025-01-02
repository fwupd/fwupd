/*
 * Copyright 2020 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include <fwupdplugin.h>

#include "fu-uefi-dbx-snapd-notifier.h"

#define FU_TYPE_UEFI_DBX_DEVICE (fu_uefi_dbx_device_get_type())
G_DECLARE_FINAL_TYPE(FuUefiDbxDevice, fu_uefi_dbx_device, FU, UEFI_DBX_DEVICE, FuDevice)

FuUefiDbxDevice *
fu_uefi_dbx_device_new(FuContext *ctx);

void
fu_uefi_dbx_device_set_snapd_notifier(FuUefiDbxDevice *self, FuUefiDbxSnapdNotifier *obs);
