/*
 * Copyright 2024 Maciej Borzecki <maciej.borzecki@canonical.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#define G_LOG_DOMAIN "FuSecureBootDevice"

#include "config.h"

#include "fu-firmware.h"
#include "fu-secure-boot-device.h"

/**
 * FuSecureBootDevice
 *
 * Is an interface implemented by devices which operate on data that may be
 * inspected or measured during secure boot process, eg. UEFI DBX, PK and
 * others.
 *
 */

G_DEFINE_INTERFACE(FuSecureBootDevice, fu_secure_boot_device, G_TYPE_OBJECT)

static void
fu_secure_boot_device_default_init(FuSecureBootDeviceInterface *iface)
{
}

/**
 * fu_secure_boot_device_get_kind:
 * @self: a #FuSecureBootDevice
 *
 * Gets the kind of device.
 *
 * Returns: #FuSecureBootDeviceKind
 *
 **/
FuSecureBootDeviceKind
fu_secure_boot_device_get_kind(FuSecureBootDevice *self)
{
	FuSecureBootDeviceInterface *iface = NULL;

	g_return_val_if_fail(FU_IS_SECURE_BOOT_DEVICE(self),
			     FU_SECURE_BOOT_DEVICE_KIND_UNSPECIFIED);

	iface = FU_SECURE_BOOT_DEVICE_GET_IFACE(self);
	return iface->get_kind(self);
}

/**
 * fu_secure_boot_device_set_firmware_write_observe:
 * @self: a #FuSecureBootDevice
 *
 * Install a callback for observing firmware writes.
 *
 * There can be only one callback registered for any secure boot object.
 *
 * Returns: TRUE if the callback was set.
 *
 **/
gboolean
fu_secure_boot_device_set_firmware_write_observe(FuSecureBootDevice *self,
						 FuSecureBootDeviceFirmwareObserveFunc func_cb,
						 gpointer user_data)
{
	FuSecureBootDeviceInterface *iface = NULL;

	/* TODO find vfunc? */
	g_return_val_if_fail(FU_IS_SECURE_BOOT_DEVICE(self), FALSE);
	iface = FU_SECURE_BOOT_DEVICE_GET_IFACE(self);
	return iface->set_firmware_write_observe(self, func_cb, user_data);
}

/**
 * fu_secure_boot_device_reset_firmware_write_observe:
 * @self: a #FuSecureBootDevice
 *
 * Reset/clear firmare write observe callback.
 *
 **/
void
fu_secure_boot_device_reset_firmware_write_observe(FuSecureBootDevice *self)
{
	FuSecureBootDeviceInterface *iface = NULL;

	g_return_if_fail(FU_IS_SECURE_BOOT_DEVICE(self));
	iface = FU_SECURE_BOOT_DEVICE_GET_IFACE(self);
	(void)iface->set_firmware_write_observe(self, NULL, NULL);
}
