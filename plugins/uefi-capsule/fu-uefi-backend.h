/*
 * Copyright (C) 2021 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#pragma once

#include <fwupdplugin.h>

#include "fu-uefi-device.h"

#define FU_TYPE_UEFI_BACKEND (fu_uefi_backend_get_type())
G_DECLARE_DERIVABLE_TYPE(FuUefiBackend, fu_uefi_backend, FU, UEFI_BACKEND, FuBackend)

struct _FuUefiBackendClass {
	FuBackendClass parent_class;
};

FuBackend *
fu_uefi_backend_new(FuContext *ctx);
void
fu_uefi_backend_set_device_gtype(FuUefiBackend *self, GType device_gtype);
GType
fu_uefi_backend_get_device_gtype(FuUefiBackend *self);

FuUefiDevice *
fu_uefi_backend_device_new_from_guid(FuUefiBackend *self, const gchar *guid);
FuUefiDevice *
fu_uefi_backend_device_new_from_dev(FuUefiBackend *self, FuDevice *dev);
