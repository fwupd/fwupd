/*
 * Copyright 2022 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include <fwupdplugin.h>

#define FU_TYPE_UEFI_CAPSULE_PLUGIN (fu_uefi_capsule_plugin_get_type())
G_DECLARE_FINAL_TYPE(FuUefiCapsulePlugin, fu_uefi_capsule_plugin, FU, UEFI_CAPSULE_PLUGIN, FuPlugin)

FuBackend *
fu_uefi_capsule_plugin_get_backend(FuUefiCapsulePlugin *self);
