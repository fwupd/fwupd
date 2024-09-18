/*
 * Copyright 2018 Richard Hughes <richard@hughsie.com>
 * Copyright 2015 Peter Jones <pjones@redhat.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include <fwupdplugin.h>

typedef enum {
	FU_UEFI_BOOTMGR_FLAG_NONE = 0,
	FU_UEFI_BOOTMGR_FLAG_USE_SHIM_FOR_SB = 1 << 0,
	FU_UEFI_BOOTMGR_FLAG_USE_SHIM_UNIQUE = 1 << 1,
	FU_UEFI_BOOTMGR_FLAG_MODIFY_BOOTORDER = 1 << 2,
	FU_UEFI_BOOTMGR_FLAG_LAST
} FuUefiBootmgrFlags;

gboolean
fu_uefi_bootmgr_verify_fwupd(FuEfivars *efivars, GError **error);
gboolean
fu_uefi_bootmgr_bootnext(FuEfivars *efivars,
			 FuVolume *esp,
			 const gchar *description,
			 FuUefiBootmgrFlags flags,
			 GError **error);
