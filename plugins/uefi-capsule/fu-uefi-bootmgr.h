/*
 * Copyright 2018 Richard Hughes <richard@hughsie.com>
 * Copyright 2015 Peter Jones <pjones@redhat.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include <fwupdplugin.h>

#include "fu-uefi-capsule-device.h"

gboolean
fu_uefi_bootmgr_verify_fwupd(FuEfivars *efivars, GError **error);
gboolean
fu_uefi_bootmgr_bootnext(FuUefiCapsuleDevice *capsule_device,
			 const gchar *description,
			 GError **error);
