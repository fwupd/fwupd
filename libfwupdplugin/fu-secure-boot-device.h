/*
 * Copyright 2024 Maciej Borzecki <maciej.borzecki@canonical.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include "fu-device.h"
#include "fu-firmware.h"
#include "glibconfig.h"

#define FU_TYPE_SECURE_BOOT_DEVICE (fu_secure_boot_device_get_type())
G_DECLARE_INTERFACE(FuSecureBootDevice, fu_secure_boot_device, FU, SECURE_BOOT_DEVICE, GObject)

typedef enum {
	FU_SECURE_BOOT_DEVICE_KIND_UNSPECIFIED = 0,
	FU_SECURE_BOOT_DEVICE_KIND_UEFI_DBX,
	/*< private >*/
	FU_SECURE_BOOT_DEVICE_KIND_LAST
} FuSecureBootDeviceKind;

/**
 * FuSecureBootDeviceFirmwareObserveFunc
 * @dev: a #FuSecureBootDevice
 * @fw: a #FuFirmware about to be written to the device
 * @user_data: a pointer to user data
 *
 * Function called by secure device right before writing the device firmware.
 * May be called multiple times in during an update. It is generally expected
 * that this is a place where a plugin may interact with other parts of the
 * system.
 *
 **/
typedef gboolean (*FuSecureBootDeviceFirmwareObserveFunc)(FuSecureBootDevice *dev,
							  FuFirmware *fw,
							  gpointer user_data,
							  GError **error) G_GNUC_WARN_UNUSED_RESULT;

struct _FuSecureBootDeviceInterface {
	GTypeInterface parent;

	/**
	 * get_kind
	 * @self: a #FuSecureBootDevice
	 *
	 * Obtain device kind.
	 *
	 * Returns: #FuSecureBootDeviceKind
	 *
	 * Since: 1.7.2
	 **/
	FuSecureBootDeviceKind (*get_kind)(FuSecureBootDevice *self);

	/**
	 * set_firmware_write_observe
	 * @self: a #FuSecureBootDevice
	 * @cb: a callback to invoke when attempting to write the firmware
	 *
	 * Set the callback to invoke for when the device firmware is about to be
	 * written. It is generally expected there will be just one callback
	 * registered for any given device. Calling with a NULL callback is
	 * equivalent to clearing it.
	 *
	 * Returns: TRUE when no callback was previously set.
	 *
	 * Since: 1.7.2
	 **/
	gboolean (*set_firmware_write_observe)(FuSecureBootDevice *self,
					       FuSecureBootDeviceFirmwareObserveFunc cb,
					       gpointer user_data);
};

FuSecureBootDeviceKind
fu_secure_boot_device_get_kind(FuSecureBootDevice *self) G_GNUC_NON_NULL(1);

gboolean
fu_secure_boot_device_set_firmware_write_observe(FuSecureBootDevice *self,
						 FuSecureBootDeviceFirmwareObserveFunc func,
						 gpointer user_data);
void
fu_secure_boot_device_reset_firmware_write_observe(FuSecureBootDevice *self);
