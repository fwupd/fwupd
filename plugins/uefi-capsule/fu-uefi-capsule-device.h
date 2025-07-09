/*
 * Copyright 2018 Richard Hughes <richard@hughsie.com>
 * Copyright 2015 Peter Jones <pjones@redhat.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include <fwupdplugin.h>

#include "fu-uefi-update-info.h"

#define FU_TYPE_UEFI_CAPSULE_DEVICE (fu_uefi_capsule_device_get_type())
G_DECLARE_DERIVABLE_TYPE(FuUefiCapsuleDevice,
			 fu_uefi_capsule_device,
			 FU,
			 UEFI_CAPSULE_DEVICE,
			 FuDevice)

struct _FuUefiCapsuleDeviceClass {
	FuDeviceClass parent_class;
};

extern GQuark FU_UEFI_CAPSULE_DEVICE_FLAG_NO_UX_CAPSULE;
extern GQuark FU_UEFI_CAPSULE_DEVICE_FLAG_USE_SHIM_UNIQUE;
extern GQuark FU_UEFI_CAPSULE_DEVICE_FLAG_USE_LEGACY_BOOTMGR_DESC;
extern GQuark FU_UEFI_CAPSULE_DEVICE_FLAG_SUPPORTS_BOOT_ORDER_LOCK;
extern GQuark FU_UEFI_CAPSULE_DEVICE_FLAG_USE_SHIM_FOR_SB;
extern GQuark FU_UEFI_CAPSULE_DEVICE_FLAG_NO_RT_SET_VARIABLE;
extern GQuark FU_UEFI_CAPSULE_DEVICE_FLAG_NO_CAPSULE_HEADER_FIXUP;
extern GQuark FU_UEFI_CAPSULE_DEVICE_FLAG_ENABLE_DEBUGGING;
extern GQuark FU_UEFI_CAPSULE_DEVICE_FLAG_COD_INDEXED_FILENAME;
extern GQuark FU_UEFI_CAPSULE_DEVICE_FLAG_MODIFY_BOOTORDER;
extern GQuark FU_UEFI_CAPSULE_DEVICE_FLAG_COD_DELL_RECOVERY;
extern GQuark FU_UEFI_CAPSULE_DEVICE_FLAG_NO_ESP_BACKUP;
extern GQuark FU_UEFI_CAPSULE_DEVICE_FLAG_USE_FWUPD_EFI;

void
fu_uefi_capsule_device_set_esp(FuUefiCapsuleDevice *self, FuVolume *esp);
gboolean
fu_uefi_capsule_device_clear_status(FuUefiCapsuleDevice *self, GError **error);
FuUefiCapsuleDeviceKind
fu_uefi_capsule_device_get_kind(FuUefiCapsuleDevice *self);
const gchar *
fu_uefi_capsule_device_get_guid(FuUefiCapsuleDevice *self);
FuVolume *
fu_uefi_capsule_device_get_esp(FuUefiCapsuleDevice *self);
gchar *
fu_uefi_capsule_device_build_varname(FuUefiCapsuleDevice *self);
guint32
fu_uefi_capsule_device_get_version(FuUefiCapsuleDevice *self);
guint32
fu_uefi_capsule_device_get_version_lowest(FuUefiCapsuleDevice *self);
guint32
fu_uefi_capsule_device_get_version_error(FuUefiCapsuleDevice *self);
guint32
fu_uefi_capsule_device_get_capsule_flags(FuUefiCapsuleDevice *self);
guint64
fu_uefi_capsule_device_get_hardware_instance(FuUefiCapsuleDevice *self);
FuUefiCapsuleDeviceStatus
fu_uefi_capsule_device_get_status(FuUefiCapsuleDevice *self);
FuUefiUpdateInfo *
fu_uefi_capsule_device_load_update_info(FuUefiCapsuleDevice *self, GError **error);
gboolean
fu_uefi_capsule_device_write_update_info(FuUefiCapsuleDevice *self,
					 const gchar *capsule_path,
					 const gchar *varname,
					 const gchar *guid,
					 GError **error);
GBytes *
fu_uefi_capsule_device_fixup_firmware(FuUefiCapsuleDevice *self, GBytes *fw, GError **error);
void
fu_uefi_capsule_device_set_status(FuUefiCapsuleDevice *self, FuUefiCapsuleDeviceStatus status);
void
fu_uefi_capsule_device_set_require_esp_free_space(FuUefiCapsuleDevice *self,
						  gsize require_esp_free_space);
gboolean
fu_uefi_capsule_device_perhaps_enable_debugging(FuUefiCapsuleDevice *self, GError **error);
FuEfiDevicePathList *
fu_uefi_capsule_device_build_dp_buf(FuVolume *esp, const gchar *capsule_path, GError **error);
gboolean
fu_uefi_capsule_device_check_asset(FuUefiCapsuleDevice *self, GError **error);
