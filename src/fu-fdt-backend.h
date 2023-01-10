/*
 * Copyright (C) 2023 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#pragma once

#include "fu-backend.h"

#define FU_TYPE_FDT_BACKEND (fu_fdt_backend_get_type())
G_DECLARE_FINAL_TYPE(FuFdtBackend, fu_fdt_backend, FU, FDT_BACKEND, FuBackend)

FuBackend *
fu_fdt_backend_new(FuContext *ctx);
