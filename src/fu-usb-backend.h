/*
 * Copyright 2021 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include "fu-backend.h"

#define FU_TYPE_USB_BACKEND (fu_usb_backend_get_type())
G_DECLARE_FINAL_TYPE(FuUsbBackend, fu_usb_backend, FU, USB_BACKEND, FuBackend)

FuBackend *
fu_usb_backend_new(FuContext *ctx) G_GNUC_NON_NULL(1);
