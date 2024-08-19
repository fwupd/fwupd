/*
 * Copyright 2018 Richard Hughes <richard@hughsie.com>
 * Copyright 2015 Peter Jones <pjones@redhat.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include <fwupdplugin.h>

#include "fu-uefi-device.h"
#include "fu-uefi-update-info.h"

#define FU_TYPE_UEFI_DEVICE (fu_uefi_device_get_type())
G_DECLARE_DERIVABLE_TYPE(FuUefiDevice, fu_uefi_device, FU, UEFI_DEVICE, FuDevice)

struct _FuUefiDeviceClass {
	FuDeviceClass parent_class;
};

void
fu_uefi_device_set_esp(FuUefiDevice *self, FuVolume *esp);
gboolean
fu_uefi_device_clear_status(FuUefiDevice *self, GError **error);
FuUefiDeviceKind
fu_uefi_device_get_kind(FuUefiDevice *self);
const gchar *
fu_uefi_device_get_guid(FuUefiDevice *self);
FuVolume *
fu_uefi_device_get_esp(FuUefiDevice *self);
gchar *
fu_uefi_device_build_varname(FuUefiDevice *self);
guint32
fu_uefi_device_get_version(FuUefiDevice *self);
guint32
fu_uefi_device_get_version_lowest(FuUefiDevice *self);
guint32
fu_uefi_device_get_version_error(FuUefiDevice *self);
guint32
fu_uefi_device_get_capsule_flags(FuUefiDevice *self);
guint64
fu_uefi_device_get_hardware_instance(FuUefiDevice *self);
FuUefiDeviceStatus
fu_uefi_device_get_status(FuUefiDevice *self);
FuUefiUpdateInfo *
fu_uefi_device_load_update_info(FuUefiDevice *self, GError **error);
gboolean
fu_uefi_device_write_update_info(FuUefiDevice *self,
				 const gchar *capsule_path,
				 const gchar *varname,
				 const gchar *guid,
				 GError **error);
GBytes *
fu_uefi_device_fixup_firmware(FuUefiDevice *self, GBytes *fw, GError **error);
void
fu_uefi_device_set_status(FuUefiDevice *self, FuUefiDeviceStatus status);
void
fu_uefi_device_set_require_esp_free_space(FuUefiDevice *self, gsize require_esp_free_space);
gboolean
fu_uefi_device_perhaps_enable_debugging(FuUefiDevice *self, GError **error);
FuEfiDevicePathList *
fu_uefi_device_build_dp_buf(FuVolume *esp, const gchar *capsule_path, GError **error);
