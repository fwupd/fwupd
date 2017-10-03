/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2017 Richard Hughes <richard@hughsie.com>
 *
 * Licensed under the GNU Lesser General Public License Version 2.1
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301 USA
 */

#include "config.h"

#include <gio/gio.h>
#include <gusb.h>

#include "fu-device-locker.h"

/**
 * SECTION:fu-device-locker
 * @title: FuDeviceLocker
 * @short_description: a device helper object
 *
 * An object that makes it easy to close a device when an object goes out of
 * scope.
 *
 * See also: #FuDevice
 */

struct _FuDeviceLocker {
	GObject			 parent_instance;
	GObject			*device;
	gboolean		 device_open;
	FuDeviceLockerFunc	 open_func;
	FuDeviceLockerFunc	 close_func;
};

G_DEFINE_TYPE (FuDeviceLocker, fu_device_locker, G_TYPE_OBJECT)

static void
fu_device_locker_finalize (GObject *obj)
{
	FuDeviceLocker *self = FU_DEVICE_LOCKER (obj);

	/* close device */
	if (self->device_open) {
		g_autoptr(GError) error = NULL;
		if (!self->close_func (self->device, &error))
			g_warning ("failed to close device: %s", error->message);
	}

	g_object_unref (self->device);
	G_OBJECT_CLASS (fu_device_locker_parent_class)->finalize (obj);
}

static void
fu_device_locker_class_init (FuDeviceLockerClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	object_class->finalize = fu_device_locker_finalize;
}

static void
fu_device_locker_init (FuDeviceLocker *self)
{
}

/**
 * fu_device_locker_new:
 * @device: A #GObject
 * @error: A #GError, or %NULL
 *
 * Opens the device for use. When the #FuDeviceLocker is deallocated the device
 * will be closed and any error will just be directed to the console.
 * This object is typically called using g_autoptr() but the device can also be
 * manually closed using g_clear_object().
 *
 * The functions used for opening and closing the device are set automatically.
 * If the @device is not a type or supertype of @GUsbDevice then this function
 * will not work. For custom objects please use fu_device_locker_new_full().
 *
 * NOTE: If the @open_func failed then the @close_func will not be called.
 *
 * Think of this object as the device ownership.
 *
 * Returns: a #FuDeviceLocker, or %NULL if the @open_func failed.
 **/
FuDeviceLocker *
fu_device_locker_new (gpointer device, GError **error)
{
	g_return_val_if_fail (device != NULL, NULL);
	g_return_val_if_fail (error != NULL, NULL);

	/* GUsbDevice */
	if (G_USB_IS_DEVICE (device)) {
		return fu_device_locker_new_full (device,
						  (FuDeviceLockerFunc) g_usb_device_open,
						  (FuDeviceLockerFunc) g_usb_device_close,
						  error);
	}
	g_set_error_literal (error,
			     G_IO_ERROR,
			     G_IO_ERROR_NOT_SUPPORTED,
			     "device object type not supported");
	return NULL;
}

/**
 * fu_device_locker_new_full:
 * @device: A #GObject
 * @open_func: (scope async): A function to open the device
 * @close_func: (scope async): A function to close the device
 * @error: A #GError, or %NULL
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
 * Returns: a #FuDeviceLocker, or %NULL if the @open_func failed.
 **/
FuDeviceLocker *
fu_device_locker_new_full (gpointer device,
			   FuDeviceLockerFunc open_func,
			   FuDeviceLockerFunc close_func,
			   GError **error)
{
	g_autoptr(FuDeviceLocker) self = NULL;

	g_return_val_if_fail (device != NULL, NULL);
	g_return_val_if_fail (open_func != NULL, NULL);
	g_return_val_if_fail (close_func != NULL, NULL);
	g_return_val_if_fail (error != NULL, NULL);

	/* create object */
	self = g_object_new (FU_TYPE_DEVICE_LOCKER, NULL);
	self->device = g_object_ref (device);
	self->open_func = open_func;
	self->close_func = close_func;

	/* open device */
	if (!self->open_func (device, error))
		return NULL;

	/* success */
	self->device_open = TRUE;
	return g_steal_pointer (&self);
}
