/*
 * Copyright 2026 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include <fwupdplugin.h>

#define FU_TYPE_STARLABS_COREBOOT_DEVICE (fu_starlabs_coreboot_device_get_type())
G_DECLARE_FINAL_TYPE(FuStarlabsCorebootDevice,
		     fu_starlabs_coreboot_device,
		     FU,
		     STARLABS_COREBOOT_DEVICE,
		     FuDevice)
