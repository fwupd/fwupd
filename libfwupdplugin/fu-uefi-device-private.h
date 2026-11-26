/*
 * Copyright 2025 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include "fu-uefi-device.h"

G_BEGIN_DECLS

FuUefiDevice *
fu_uefi_device_new(FuContext *ctx, FuEfiSecureBootSiglistKind kind) G_GNUC_NON_NULL(1);

G_END_DECLS
