/*
 * Copyright (C) 2021 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#pragma once

#include "fu-backend.h"

#define FU_TYPE_UEFI_BACKEND (fu_uefi_backend_get_type ())
G_DECLARE_FINAL_TYPE (FuUefiBackend, fu_uefi_backend, FU, UEFI_BACKEND, FuBackend)

FuBackend	*fu_uefi_backend_new			(FuContext	*ctx);
