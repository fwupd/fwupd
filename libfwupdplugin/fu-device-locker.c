/*
 * Copyright 2017 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#define G_LOG_DOMAIN "FuDeviceLocker"

#include "config.h"

#include <gio/gio.h>

#include "fu-device-locker.h"
#include "fu-device.h"

/**
 * FuDeviceLocker:
 *
 * Easily close a shared resource (such as a device) when an object goes out of
 * scope.
 *
 * See also: [class@FuDevice]
 */

struct _FuDeviceLocker {
	GObject parent_instance;
	GObject *device;
	gboolean device_open;
	FuDeviceLockerFunc open_func;
	FuDeviceLockerFunc close_func;
};

G_DEFINE_TYPE(FuDeviceLocker, fu_device_locker, G_TYPE_OBJECT)

static void
fu_device_locker_finalize(GObject *obj)
{
	FuDeviceLocker *self = FU_DEVICE_LOCKER(obj);

	/* close device */
	if (self->device_open) {
		g_autoptr(GError) error = NULL;
		if (!self->close_func(self->device, &error))
			g_warning("failed to close device: %s", error->message);
	}
	if (self->device != NULL)
		g_object_unref(self->device);
	G_OBJECT_CLASS(fu_device_locker_parent_class)->finalize(obj);
}

static void
fu_device_locker_class_init(FuDeviceLockerClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS(klass);
	object_class->finalize = fu_device_locker_finalize;
}

static void
fu_device_locker_init(FuDeviceLocker *self)
{
}

/**
 * fu_device_locker_close:
 * @self: a #FuDeviceLocker
 * @error: (nullable): optional return location for an error
 *
 * Closes the locker before it gets cleaned up.
 *
 * This function can be used to manually close a device managed by a locker,
 * and allows the caller to properly handle the error.
 *
 * Returns: %TRUE for success
 *
 * Since: 1.4.0
 **/
gboolean
fu_device_locker_close(FuDeviceLocker *self, GError **error)
{
	g_autoptr(GError) error_local = NULL;
	g_return_val_if_fail(FU_IS_DEVICE_LOCKER(self), FALSE);
	g_return_val_if_fail(error == NULL || *error == NULL, FALSE);
	if (!self->device_open)
		return TRUE;
	if (!self->close_func(self->device, &error_local)) {
		if (g_error_matches(error_local, FWUPD_ERROR, FWUPD_ERROR_NOT_FOUND)) {
			g_debug("ignoring: %s", error_local->message);
			return TRUE;
		}
		g_propagate_error(error, g_steal_pointer(&error_local));
		return FALSE;
	}
	self->device_open = FALSE;
	return TRUE;
}

/**
 * fu_device_locker_new:
 * @device: a #GObject
 * @error: (nullable): optional return location for an error
 *
 * Opens the device for use. When the #FuDeviceLocker is deallocated the device
 * will be closed and any error will just be directed to the console.
 * This object is typically called using g_autoptr() but the device can also be
 * manually closed using g_clear_object().
 *
 * The functions used for opening and closing the device are set automatically.
 * If the @device is not a type or supertype of #FuDevice then this function will not work.
 *
 * For custom objects please use fu_device_locker_new_full().
 *
 * NOTE: If the @open_func failed then the @close_func will not be called.
 *
 * Think of this object as the device ownership.
 *
 * Returns: a device locker, or %NULL if the @open_func failed.
 *
 * Since: 1.0.0
 **/
FuDeviceLocker *
fu_device_locker_new(gpointer device, GError **error)
{
	g_return_val_if_fail(device != NULL, NULL);
	g_return_val_if_fail(error == NULL || *error == NULL, NULL);

	/* FuDevice */
	if (FU_IS_DEVICE(device)) {
		return fu_device_locker_new_full(device,
						 (FuDeviceLockerFunc)fu_device_open,
						 (FuDeviceLockerFunc)fu_device_close,
						 error);
	}
	g_set_error_literal(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_NOT_SUPPORTED,
			    "device object type not supported");
	return NULL;
}

/**
 * fu_device_locker_new_full:
 * @device: a #GObject
 * @open_func: (scope async): a function to open the device
 * @close_func: (scope async): a function to close the device
 * @error: (nullable): optional return location for an error
 *
 * Opens the device for use. When the #FuDeviceLocker is deallocated the device
 * will be closed and any error will just be directed to the console.
 * This object is typically called using g_autoptr() but the device can also be
 * manually closed using g_clear_object().
 *
 * NOTE: If the @open_func failed then the @close_func will not be called.
 *
 * Think of this object as the device ownership.
 *
 * Returns: a device locker, or %NULL if the @open_func failed.
 *
 * Since: 1.0.0
 **/
FuDeviceLocker *
fu_device_locker_new_full(gpointer device,
			  FuDeviceLockerFunc open_func,
			  FuDeviceLockerFunc close_func,
			  GError **error)
{
	g_autoptr(FuDeviceLocker) self = NULL;

	g_return_val_if_fail(device != NULL, NULL);
	g_return_val_if_fail(open_func != NULL, NULL);
	g_return_val_if_fail(close_func != NULL, NULL);
	g_return_val_if_fail(error == NULL || *error == NULL, NULL);

	/* create object */
	self = g_object_new(FU_TYPE_DEVICE_LOCKER, NULL);
	self->device = g_object_ref(device);
	self->open_func = open_func;
	self->close_func = close_func;

	/* open device */
	if (!self->open_func(device, error)) {
		g_autoptr(GError) error_local = NULL;
		if (!self->close_func(device, &error_local)) {
			if (error_local != NULL) {
				if (!g_error_matches(error_local,
						     FWUPD_ERROR,
						     FWUPD_ERROR_NOTHING_TO_DO)) {
					g_debug("ignoring close error on aborted open: %s",
						error_local->message);
				}
			}
		}
		g_object_unref(device);
		return NULL;
	}

	/* success */
	self->device_open = TRUE;
	return g_steal_pointer(&self);
}
