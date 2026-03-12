/*
 * Copyright 2025 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include "fu-uefi-device.h"

FuUefiDevice *
fu_uefi_device_new(const gchar *guid, const gchar *name) G_GNUC_NON_NULL(1, 2);
void
fu_uefi_device_set_guid(FuUefiDevice *self, const gchar *guid) G_GNUC_NON_NULL(1);
const gchar *
fu_uefi_device_get_guid(FuUefiDevice *self) G_GNUC_NON_NULL(1);
void
fu_uefi_device_set_name(FuUefiDevice *self, const gchar *name) G_GNUC_NON_NULL(1);
const gchar *
fu_uefi_device_get_name(FuUefiDevice *self) G_GNUC_NON_NULL(1);
