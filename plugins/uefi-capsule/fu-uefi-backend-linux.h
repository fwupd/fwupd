/*
 * Copyright (C) 2021 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#pragma once

#include <fwupdplugin.h>

#include "fu-uefi-backend.h"

#define FU_TYPE_UEFI_BACKEND_LINUX (fu_uefi_backend_linux_get_type())
G_DECLARE_FINAL_TYPE(FuUefiBackendLinux,
		     fu_uefi_backend_linux,
		     FU,
		     UEFI_BACKEND_LINUX,
		     FuUefiBackend)
