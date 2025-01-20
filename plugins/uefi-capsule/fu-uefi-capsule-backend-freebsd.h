/*
 * Copyright 2021 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include <fwupdplugin.h>

#include "fu-uefi-capsule-backend.h"

#define FU_TYPE_UEFI_CAPSULE_BACKEND_FREEBSD (fu_uefi_capsule_backend_freebsd_get_type())
G_DECLARE_FINAL_TYPE(FuUefiCapsuleBackendFreebsd,
		     fu_uefi_capsule_backend_freebsd,
		     FU,
		     UEFI_CAPSULE_BACKEND_FREEBSD,
		     FuUefiCapsuleBackend)
