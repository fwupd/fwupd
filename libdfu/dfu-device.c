/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2015 Richard Hughes <richard@hughsie.com>
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

/**
 * SECTION:dfu-device
 * @short_description: Object representing a DFU device
 *
 * This object allows reading and writing DFU-suffix files.
 */

#include "config.h"

#include <fwupd.h>
#include <string.h>

#include "dfu-common.h"
#include "dfu-device.h"
#include "dfu-target-private.h"

static void dfu_device_finalize			 (GObject *object);

/**
 * DfuDevicePrivate:
 *
 * Private #DfuDevice data
 **/
typedef struct {
	GPtrArray		*targets;
} DfuDevicePrivate;

G_DEFINE_TYPE_WITH_PRIVATE (DfuDevice, dfu_device, G_TYPE_OBJECT)
#define GET_PRIVATE(o) (dfu_device_get_instance_private (o))

/**
 * dfu_device_class_init:
 **/
static void
dfu_device_class_init (DfuDeviceClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	object_class->finalize = dfu_device_finalize;
}

/**
 * dfu_device_init:
 **/
static void
dfu_device_init (DfuDevice *device)
{
	DfuDevicePrivate *priv = GET_PRIVATE (device);
	priv->targets = g_ptr_array_new_with_free_func ((GDestroyNotify) g_object_unref);
}

/**
 * dfu_device_finalize:
 **/
static void
dfu_device_finalize (GObject *object)
{
	DfuDevice *device = DFU_DEVICE (object);
	DfuDevicePrivate *priv = GET_PRIVATE (device);

	g_ptr_array_unref (priv->targets);

	G_OBJECT_CLASS (dfu_device_parent_class)->finalize (object);
}

/**
 * dfu_device_set_dev:
 **/
static gboolean
dfu_device_set_dev (DfuDevice *device, GUsbDevice *dev)
{
	DfuDevicePrivate *priv = GET_PRIVATE (device);
	guint i;
	GUsbInterface *iface;
	g_autoptr(GPtrArray) ifaces = NULL;

	/* add all DFU-capable targets */
	ifaces = g_usb_device_get_interfaces (dev, NULL);
	if (ifaces == NULL)
		return FALSE;
	g_ptr_array_set_size (priv->targets, 0);
	for (i = 0; i < ifaces->len; i++) {
		DfuTarget *target;
		iface = g_ptr_array_index (ifaces, i);
		if (g_usb_interface_get_class (iface) != G_USB_DEVICE_CLASS_APPLICATION_SPECIFIC)
			continue;
		if (g_usb_interface_get_subclass (iface) != 0x01)
			continue;
		target = dfu_target_new (dev, iface);
		if (target == NULL)
			continue;
		g_ptr_array_add (priv->targets, target);
	}
	return priv->targets->len > 0;
}

/**
 * dfu_device_new:
 *
 * Creates a new DFU device object.
 *
 * Return value: a new #DfuDevice, or %NULL if @dev was not DFU-capable
 *
 * Since: 0.5.4
 **/
DfuDevice *
dfu_device_new (GUsbDevice *dev)
{
	DfuDevice *device;
	device = g_object_new (DFU_TYPE_DEVICE, NULL);
	if (!dfu_device_set_dev (device, dev)) {
		g_object_unref (device);
		return NULL;
	}
	return device;
}

/**
 * dfu_device_get_targets:
 * @device: a #DfuDevice
 *
 * Gets all the targets for this device.
 *
 * Return value: (transfer none): (element-type DfuTarget): #DfuTarget, or %NULL
 *
 * Since: 0.5.4
 **/
GPtrArray *
dfu_device_get_targets (DfuDevice *device)
{
	DfuDevicePrivate *priv = GET_PRIVATE (device);
	g_return_val_if_fail (DFU_IS_DEVICE (device), NULL);
	return priv->targets;
}

/**
 * dfu_device_get_target_by_alt_setting:
 * @device: a #DfuDevice
 * @alt_setting: the setting used to find
 *
 * Gets a target with a specific alternative setting.
 *
 * Return value: (transfer full): a #DfuTarget, or %NULL
 *
 * Since: 0.5.4
 **/
DfuTarget *
dfu_device_get_target_by_alt_setting (DfuDevice *device, guint8 alt_setting, GError **error)
{
	DfuDevicePrivate *priv = GET_PRIVATE (device);
	DfuTarget *target;
	guint i;

	g_return_val_if_fail (DFU_IS_DEVICE (device), NULL);
	g_return_val_if_fail (error == NULL || *error == NULL, NULL);

	/* find by ID */
	for (i = 0; i < priv->targets->len; i++) {
		target = g_ptr_array_index (priv->targets, i);
		if (dfu_target_get_interface_alt_setting (target) == alt_setting)
			return g_object_ref (target);
	}

	/* failed */
	g_set_error (error,
		     FWUPD_ERROR,
		     FWUPD_ERROR_INTERNAL,
		     "No target with alt-setting %i",
		     alt_setting);
	return NULL;
}

/**
 * dfu_device_get_target_by_alt_name:
 * @device: a #DfuDevice
 * @alt_name: the name used to find
 *
 * Gets a target with a specific alternative name.
 *
 * Return value: (transfer full): a #DfuTarget, or %NULL
 *
 * Since: 0.5.4
 **/
DfuTarget *
dfu_device_get_target_by_alt_name (DfuDevice *device, const gchar *alt_name, GError **error)
{
	DfuDevicePrivate *priv = GET_PRIVATE (device);
	DfuTarget *target;
	guint i;

	g_return_val_if_fail (DFU_IS_DEVICE (device), NULL);
	g_return_val_if_fail (error == NULL || *error == NULL, NULL);

	/* find by ID */
	for (i = 0; i < priv->targets->len; i++) {
		target = g_ptr_array_index (priv->targets, i);
		if (g_strcmp0 (dfu_target_get_interface_alt_name (target), alt_name) == 0)
			return g_object_ref (target);
	}

	/* failed */
	g_set_error (error,
		     FWUPD_ERROR,
		     FWUPD_ERROR_INTERNAL,
		     "No target with alt-name %s",
		     alt_name);
	return NULL;
}
