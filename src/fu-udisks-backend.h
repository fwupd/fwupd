/*
 * Copyright (C) 2021 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#pragma once

#include "fu-backend.h"

#define FU_TYPE_UDISKS_BACKEND (fu_udisks_backend_get_type())
G_DECLARE_FINAL_TYPE(FuUdisksBackend, fu_udisks_backend, FU, UDISKS_BACKEND, FuBackend)

FuBackend *
fu_udisks_backend_new(void);
