/*
 * Copyright (C) 2021
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#pragma once

#include "fu-backend.h"

#define FU_TYPE_BLUEZ_BACKEND (fu_bluez_backend_get_type ())
G_DECLARE_FINAL_TYPE (FuBluezBackend, fu_bluez_backend, FU, BLUEZ_BACKEND, FuBackend)

FuBackend	*fu_bluez_backend_new			(void);
