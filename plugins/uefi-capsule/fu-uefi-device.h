/*
 * Copyright (C) 2018 Richard Hughes <richard@hughsie.com>
 * Copyright (C) 2015 Peter Jones <pjones@redhat.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
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

typedef enum {
	FU_UEFI_DEVICE_KIND_UNKNOWN,
	FU_UEFI_DEVICE_KIND_SYSTEM_FIRMWARE,
	FU_UEFI_DEVICE_KIND_DEVICE_FIRMWARE,
	FU_UEFI_DEVICE_KIND_UEFI_DRIVER,
	FU_UEFI_DEVICE_KIND_FMP,
	FU_UEFI_DEVICE_KIND_DELL_TPM_FIRMWARE,
	FU_UEFI_DEVICE_KIND_LAST
} FuUefiDeviceKind;

typedef enum {
	FU_UEFI_DEVICE_STATUS_SUCCESS = 0x00,
	FU_UEFI_DEVICE_STATUS_ERROR_UNSUCCESSFUL = 0x01,
	FU_UEFI_DEVICE_STATUS_ERROR_INSUFFICIENT_RESOURCES = 0x02,
	FU_UEFI_DEVICE_STATUS_ERROR_INCORRECT_VERSION = 0x03,
	FU_UEFI_DEVICE_STATUS_ERROR_INVALID_FORMAT = 0x04,
	FU_UEFI_DEVICE_STATUS_ERROR_AUTH_ERROR = 0x05,
	FU_UEFI_DEVICE_STATUS_ERROR_PWR_EVT_AC = 0x06,
	FU_UEFI_DEVICE_STATUS_ERROR_PWR_EVT_BATT = 0x07,
	FU_UEFI_DEVICE_STATUS_LAST
} FuUefiDeviceStatus;

/**
 * FU_UEFI_DEVICE_FLAG_NO_UX_CAPSULE:
 *
 * No not use the additional UX capsule.
 */
#define FU_UEFI_DEVICE_FLAG_NO_UX_CAPSULE (1 << 0)
/**
 * FU_UEFI_DEVICE_FLAG_USE_SHIM_UNIQUE:
 *
 * Use a unique shim filename to work around a common BIOS bug.
 */
#define FU_UEFI_DEVICE_FLAG_USE_SHIM_UNIQUE (1 << 1)
/**
 * FU_UEFI_DEVICE_FLAG_USE_LEGACY_BOOTMGR_DESC:
 *
 * Use the legacy boot manager description to work around a Lenovo BIOS bug.
 */
#define FU_UEFI_DEVICE_FLAG_USE_LEGACY_BOOTMGR_DESC (1 << 2)
/**
 * FU_UEFI_DEVICE_FLAG_SUPPORTS_BOOT_ORDER_LOCK:
 *
 * The BIOS might have Boot Order Lock enabled which can cause failures when
 * not using grub chainloading or capsule-on-disk.
 */
#define FU_UEFI_DEVICE_FLAG_SUPPORTS_BOOT_ORDER_LOCK (1 << 3)
/**
 * FU_UEFI_DEVICE_FLAG_FALLBACK_TO_REMOVABLE_PATH:
 *
 * Try to fallback to use UEFI removable path if the shim path doesn't exist.
 */
#define FU_UEFI_DEVICE_FLAG_FALLBACK_TO_REMOVABLE_PATH (1 << 4)
/**
 * FU_UEFI_DEVICE_FLAG_USE_SHIM_FOR_SB:
 *
 * Use shim to load fwupdx64.efi when SecureBoot is turned on.
 */
#define FU_UEFI_DEVICE_FLAG_USE_SHIM_FOR_SB (1 << 5)
/**
 * FU_UEFI_DEVICE_FLAG_NO_RT_SET_VARIABLE:
 *
 * Do not use RT->SetVariable.
 */
#define FU_UEFI_DEVICE_FLAG_NO_RT_SET_VARIABLE (1 << 6)
/**
 * FU_UEFI_DEVICE_FLAG_NO_CAPSULE_HEADER_FIXUP:
 *
 * Do not prepend a plausible missing capsule header.
 */
#define FU_UEFI_DEVICE_FLAG_NO_CAPSULE_HEADER_FIXUP (1 << 7)

FuUefiDeviceKind
fu_uefi_device_kind_from_string(const gchar *kind);

void
fu_uefi_device_set_esp(FuUefiDevice *self, FuVolume *esp);
gboolean
fu_uefi_device_clear_status(FuUefiDevice *self, GError **error);
FuUefiDeviceKind
fu_uefi_device_get_kind(FuUefiDevice *self);
const gchar *
fu_uefi_device_get_guid(FuUefiDevice *self);
gchar *
fu_uefi_device_get_esp_path(FuUefiDevice *self);
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
const gchar *
fu_uefi_device_kind_to_string(FuUefiDeviceKind kind);
const gchar *
fu_uefi_device_status_to_string(FuUefiDeviceStatus status);
FuUefiUpdateInfo *
fu_uefi_device_load_update_info(FuUefiDevice *self, GError **error);
gboolean
fu_uefi_device_write_update_info(FuUefiDevice *self,
				 const gchar *filename,
				 const gchar *varname,
				 const gchar *guid,
				 GError **error);
GBytes *
fu_uefi_device_fixup_firmware(FuUefiDevice *self, GBytes *fw, GError **error);
void
fu_uefi_device_set_status(FuUefiDevice *self, FuUefiDeviceStatus status);
