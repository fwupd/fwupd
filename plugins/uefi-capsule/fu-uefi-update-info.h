/*
 * Copyright 2018 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include <glib-object.h>

#include "fu-uefi-struct.h"

#define FU_TYPE_UEFI_UPDATE_INFO (fu_uefi_update_info_get_type())
G_DECLARE_FINAL_TYPE(FuUefiUpdateInfo, fu_uefi_update_info, FU, UEFI_UPDATE_INFO, FuFirmware)

FuUefiUpdateInfo *
fu_uefi_update_info_new(void);
const gchar *
fu_uefi_update_info_get_guid(FuUefiUpdateInfo *self);
void
fu_uefi_update_info_set_guid(FuUefiUpdateInfo *self, const gchar *guid);
const gchar *
fu_uefi_update_info_get_capsule_fn(FuUefiUpdateInfo *self);
void
fu_uefi_update_info_set_capsule_fn(FuUefiUpdateInfo *self, const gchar *capsule_fn);
void
fu_uefi_update_info_set_capsule_flags(FuUefiUpdateInfo *self, guint32 capsule_flags);
guint32
fu_uefi_update_info_get_capsule_flags(FuUefiUpdateInfo *self);
guint64
fu_uefi_update_info_get_hw_inst(FuUefiUpdateInfo *self);
void
fu_uefi_update_info_set_hw_inst(FuUefiUpdateInfo *self, guint64 hw_inst);
FuUefiUpdateInfoStatus
fu_uefi_update_info_get_status(FuUefiUpdateInfo *self);
void
fu_uefi_update_info_set_status(FuUefiUpdateInfo *self, FuUefiUpdateInfoStatus status);
