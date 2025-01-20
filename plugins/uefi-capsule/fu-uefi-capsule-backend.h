/*
 * Copyright 2021 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include <fwupdplugin.h>

#include "fu-uefi-capsule-device.h"

#define FU_TYPE_UEFI_CAPSULE_BACKEND (fu_uefi_capsule_backend_get_type())
G_DECLARE_DERIVABLE_TYPE(FuUefiCapsuleBackend,
			 fu_uefi_capsule_backend,
			 FU,
			 UEFI_CAPSULE_BACKEND,
			 FuBackend)

struct _FuUefiCapsuleBackendClass {
	FuBackendClass parent_class;
};

FuBackend *
fu_uefi_capsule_backend_new(FuContext *ctx);
void
fu_uefi_capsule_backend_set_device_gtype(FuUefiCapsuleBackend *self, GType device_gtype);
GType
fu_uefi_capsule_backend_get_device_gtype(FuUefiCapsuleBackend *self);

FuUefiCapsuleDevice *
fu_uefi_capsule_backend_device_new_from_dev(FuUefiCapsuleBackend *self, FuDevice *dev);
