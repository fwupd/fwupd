/*
 * Copyright (C) 2018 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#pragma once

#include <glib-object.h>

#include "fu-uefi-struct.h"

#define FU_TYPE_UEFI_UPDATE_INFO (fu_uefi_update_info_get_type())
G_DECLARE_FINAL_TYPE(FuUefiUpdateInfo, fu_uefi_update_info, FU, UEFI_UPDATE_INFO, GObject)

FuUefiUpdateInfo *
fu_uefi_update_info_new(void);
gboolean
fu_uefi_update_info_parse(FuUefiUpdateInfo *self, const guint8 *buf, gsize sz, GError **error);
guint32
fu_uefi_update_info_get_version(FuUefiUpdateInfo *self);
const gchar *
fu_uefi_update_info_get_guid(FuUefiUpdateInfo *self);
const gchar *
fu_uefi_update_info_get_capsule_fn(FuUefiUpdateInfo *self);
guint32
fu_uefi_update_info_get_capsule_flags(FuUefiUpdateInfo *self);
guint64
fu_uefi_update_info_get_hw_inst(FuUefiUpdateInfo *self);
FuUefiUpdateInfoStatus
fu_uefi_update_info_get_status(FuUefiUpdateInfo *self);
