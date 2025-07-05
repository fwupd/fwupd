/*
 * Copyright 2025 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include <fwupdplugin.h>

#define FU_TYPE_MM_BACKEND (fu_mm_backend_get_type())
G_DECLARE_FINAL_TYPE(FuMmBackend, fu_mm_backend, FU, MM_BACKEND, FuBackend)

FuBackend *
fu_mm_backend_new(FuContext *ctx);
