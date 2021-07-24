/*
 * Copyright (C) 2021 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#pragma once

#include <fwupdplugin.h>

#include "fu-uefi-backend.h"

#define FU_TYPE_UEFI_BACKEND_FREEBSD (fu_uefi_backend_freebsd_get_type())
G_DECLARE_FINAL_TYPE(FuUefiBackendFreebsd,
		     fu_uefi_backend_freebsd,
		     FU,
		     UEFI_BACKEND_FREEBSD,
		     FuUefiBackend)
