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
 * SECTION:dfu-context
 * @short_description: A system context for managing DFU-capable devices
 *
 * This object allows discovering and monitoring hotpluggable DFU devices.
 *
 * When using #DfuContext the device is given some time to re-enumerate after a
 * detach or reset. This allows client programs to continue using the #DfuDevice
 * without dealing with the device hotplug and the #GUsbDevice changing.
 * Using this object may be easier than using GUsbContext directly.
 *
 * Please be aware that after device detach or reset the number of #DfuTarget
 * objects may be different and so need to be re-requested.
 *
 * See also: #DfuDevice, #DfuTarget
 */

#include "config.h"

#include "dfu-device-private.h"
#include "dfu-error.h"
#include "dfu-context.h"

static void dfu_context_finalize			 (GObject *object);

typedef struct {
	GUsbContext		*usb_ctx;
	GPtrArray		*devices;		/* of DfuContextItem */
	guint			 timeout;		/* in ms */
} DfuContextPrivate;

typedef struct {
	DfuContext		*context;		/* not refcounted */
	DfuDevice		*device;		/* not refcounted */
	guint			 timeout_id;
	gulong			 state_change_id;
} DfuContextItem;

enum {
	SIGNAL_DEVICE_ADDED,
	SIGNAL_DEVICE_REMOVED,
	SIGNAL_DEVICE_CHANGED,
	SIGNAL_LAST
};

static guint signals [SIGNAL_LAST] = { 0 };

G_DEFINE_TYPE_WITH_PRIVATE (DfuContext, dfu_context, G_TYPE_OBJECT)
#define GET_PRIVATE(o) (dfu_context_get_instance_private (o))

static void
dfu_context_device_free (DfuContextItem *item)
{
	if (item->timeout_id > 0)
		g_source_remove (item->timeout_id);
	if (item->timeout_id > 0) {
		g_signal_handler_disconnect (item->device,
					     item->state_change_id);
	}
	g_object_unref (item->device);
	g_free (item);
}

static void
dfu_context_class_init (DfuContextClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);

	/**
	 * DfuContext::device-added:
	 * @context: the #DfuContext instance that emitted the signal
	 * @device: the #DfuDevice
	 *
	 * The ::device-added signal is emitted when a new DFU device is connected.
	 *
	 * Since: 0.5.4
	 **/
	signals [SIGNAL_DEVICE_ADDED] =
		g_signal_new ("device-added",
			      G_TYPE_FROM_CLASS (object_class), G_SIGNAL_RUN_LAST,
			      G_STRUCT_OFFSET (DfuContextClass, device_added),
			      NULL, NULL, g_cclosure_marshal_VOID__OBJECT,
			      G_TYPE_NONE, 1, DFU_TYPE_DEVICE);

	/**
	 * DfuContext::device-removed:
	 * @context: the #DfuContext instance that emitted the signal
	 * @device: the #DfuDevice
	 *
	 * The ::device-removed signal is emitted when a DFU device is removed.
	 *
	 * Since: 0.5.4
	 **/
	signals [SIGNAL_DEVICE_REMOVED] =
		g_signal_new ("device-removed",
			      G_TYPE_FROM_CLASS (object_class), G_SIGNAL_RUN_LAST,
			      G_STRUCT_OFFSET (DfuContextClass, device_removed),
			      NULL, NULL, g_cclosure_marshal_VOID__OBJECT,
			      G_TYPE_NONE, 1, DFU_TYPE_DEVICE);

	/**
	 * DfuContext::device-changed:
	 * @context: the #DfuContext instance that emitted the signal
	 * @device: the #DfuDevice
	 *
	 * The ::device-changed signal is emitted when a DFU device is changed,
	 * typically when it has detached or been reset.
	 *
	 * Since: 0.5.4
	 **/
	signals [SIGNAL_DEVICE_CHANGED] =
		g_signal_new ("device-changed",
			      G_TYPE_FROM_CLASS (object_class), G_SIGNAL_RUN_LAST,
			      G_STRUCT_OFFSET (DfuContextClass, device_changed),
			      NULL, NULL, g_cclosure_marshal_VOID__OBJECT,
			      G_TYPE_NONE, 1, DFU_TYPE_DEVICE);

	object_class->finalize = dfu_context_finalize;
}

static gchar *
dfu_context_get_device_id (DfuDevice *device)
{
	GUsbDevice *dev;
	dev = dfu_device_get_usb_dev (device);
	if (dev == NULL)
		return g_strdup (dfu_device_get_platform_id (device));
	return g_strdup_printf ("%04x:%04x [%s]",
				g_usb_device_get_vid (dev),
				g_usb_device_get_pid (dev),
				g_usb_device_get_platform_id (dev));
}

static DfuContextItem *
dfu_context_find_item_by_platform_id (DfuContext *context, const gchar *platform_id)
{
	DfuContextPrivate *priv = GET_PRIVATE (context);
	DfuContextItem *item;
	guint i;

	/* do we have this device */
	for (i = 0; i < priv->devices->len; i++) {
		item = g_ptr_array_index (priv->devices, i);
		if (g_strcmp0 (dfu_device_get_platform_id (item->device), platform_id) == 0)
			return item;
	}
	return NULL;
}

static void
dfu_context_remove_item (DfuContextItem *item)
{
	DfuContextPrivate *priv = GET_PRIVATE (item->context);
	g_autofree gchar *device_id = NULL;

	/* log something */
	device_id = dfu_context_get_device_id (item->device);
	g_debug ("%s was removed", device_id);

	g_signal_emit (item->context, signals[SIGNAL_DEVICE_REMOVED], 0, item->device);
	g_ptr_array_remove (priv->devices, item);
}

static gboolean
dfu_context_device_timeout_cb (gpointer user_data)
{
	DfuContextItem *item = (DfuContextItem *) user_data;
	g_autofree gchar *device_id = NULL;

	/* bad firmware? */
	device_id = dfu_context_get_device_id (item->device);
	g_debug ("%s did not come back as a DFU capable device", device_id);
	dfu_context_remove_item (item);
	return FALSE;
}

static void
dfu_context_device_state_cb (DfuDevice *device, DfuState state, DfuContext *context)
{
	g_autofree gchar *device_id = NULL;
	device_id = dfu_context_get_device_id (device);
	g_debug ("%s state now: %s", device_id, dfu_state_to_string (state));
	g_signal_emit (context, signals[SIGNAL_DEVICE_CHANGED], 0, device);
}

static void
dfu_context_device_added_cb (GUsbContext *usb_context,
			     GUsbDevice *usb_device,
			     DfuContext *context)
{
	DfuContextPrivate *priv = GET_PRIVATE (context);
	DfuDevice *device;
	DfuContextItem *item;
	const gchar *platform_id;
	g_autofree gchar *device_id = NULL;
	g_autoptr(GError) error = NULL;

	/* are we waiting for this device to come back? */
	platform_id = g_usb_device_get_platform_id (usb_device);
	item = dfu_context_find_item_by_platform_id (context, platform_id);
	if (item != NULL) {
		device_id = dfu_context_get_device_id (item->device);
		if (item->timeout_id > 0) {
			g_debug ("cancelling the remove timeout");
			g_source_remove (item->timeout_id);
			item->timeout_id  = 0;
		}

		/* try and be helpful; we may be a daemon like fwupd watching a
		 * DFU device after dfu-tool or dfu-util has detached the
		 * device on th command line */
		if (!dfu_device_set_new_usb_dev (item->device, usb_device, NULL, &error))
			g_warning ("Failed to set new device: %s", error->message);

		/* inform the UI */
		g_signal_emit (context, signals[SIGNAL_DEVICE_CHANGED], 0, item->device);
		g_debug ("device %s came back", device_id);
		return;
	}

	/* is this a DFU-capable device */
	device = dfu_device_new (usb_device);
	if (device == NULL) {
		g_debug ("device was not DFU capable");
		return;
	}

	/* add */
	item = g_new0 (DfuContextItem, 1);
	item->context = context;
	item->device = device;
	item->state_change_id =
		g_signal_connect (item->device, "state-changed",
				  G_CALLBACK (dfu_context_device_state_cb), context);
	g_ptr_array_add (priv->devices, item);
	g_signal_emit (context, signals[SIGNAL_DEVICE_ADDED], 0, device);
	device_id = dfu_context_get_device_id (item->device);
	g_debug ("device %s was added", device_id);
}

static void
dfu_context_device_removed_cb (GUsbContext *usb_context,
			       GUsbDevice  *usb_device,
			       DfuContext *context)
{
	DfuContextPrivate *priv = GET_PRIVATE (context);
	DfuContextItem *item;
	const gchar *platform_id;

	/* find the item */
	platform_id = g_usb_device_get_platform_id (usb_device);
	item = dfu_context_find_item_by_platform_id (context, platform_id);
	if (item == NULL)
		return;

	/* mark the backing USB device as invalid */
	dfu_device_set_new_usb_dev (item->device, NULL, NULL, NULL);

	/* this item has just detached */
	if (item->timeout_id > 0)
		g_source_remove (item->timeout_id);
	item->timeout_id =
		g_timeout_add (priv->timeout, dfu_context_device_timeout_cb, item);
}

static void
dfu_context_set_usb_context (DfuContext *context, GUsbContext *usb_ctx)
{
	DfuContextPrivate *priv = GET_PRIVATE (context);
	priv->usb_ctx = g_object_ref (usb_ctx);
	g_signal_connect (priv->usb_ctx, "device-added",
			  G_CALLBACK (dfu_context_device_added_cb), context);
	g_signal_connect (priv->usb_ctx, "device-removed",
			  G_CALLBACK (dfu_context_device_removed_cb), context);
}

static void
dfu_context_init (DfuContext *context)
{
	DfuContextPrivate *priv = GET_PRIVATE (context);
	priv->timeout = 5000;
	priv->devices = g_ptr_array_new_with_free_func ((GDestroyNotify) dfu_context_device_free);
}

static void
dfu_context_finalize (GObject *object)
{
	DfuContext *context = DFU_CONTEXT (object);
	DfuContextPrivate *priv = GET_PRIVATE (context);

	g_ptr_array_unref (priv->devices);
	g_object_unref (priv->usb_ctx);

	G_OBJECT_CLASS (dfu_context_parent_class)->finalize (object);
}

/**
 * dfu_context_new:
 *
 * Creates a new DFU context object.
 *
 * Return value: a new #DfuContext
 *
 * Since: 0.5.4
 **/
DfuContext *
dfu_context_new (void)
{
	DfuContext *context;
	g_autoptr(GUsbContext) usb_ctx = g_usb_context_new (NULL);
	context = g_object_new (DFU_TYPE_CONTEXT, NULL);
	dfu_context_set_usb_context (context, usb_ctx);
	return context;
}

/**
 * dfu_context_new_with_context:
 * @usb_ctx: a #DfuContext
 *
 * Creates a new DFU context object.
 *
 * Return value: a new #DfuContext
 *
 * Since: 0.7.6
 **/
DfuContext *
dfu_context_new_with_context (GUsbContext *usb_ctx)
{
	DfuContext *context;
	g_return_val_if_fail (G_USB_IS_CONTEXT (usb_ctx), NULL);
	context = g_object_new (DFU_TYPE_CONTEXT, NULL);
	dfu_context_set_usb_context (context, usb_ctx);
	return context;
}

/**
 * dfu_context_get_timeout:
 * @context: a #DfuContext
 *
 * Gets the wait-for-replug timeout.
 *
 * Return value: value in milliseconds
 *
 * Since: 0.5.4
 **/
guint
dfu_context_get_timeout (DfuContext *context)
{
	DfuContextPrivate *priv = GET_PRIVATE (context);
	g_return_val_if_fail (DFU_IS_CONTEXT (context), 0);
	return priv->timeout;
}


/**
 * dfu_context_set_timeout:
 * @context: a #DfuContext
 * @timeout: a timeout in milliseconds
 *
 * Sets the wait-for-replug timeout.
 * This is the longest we will wait for a device to re-enumerate after
 * disconnecting. Using longer values will result in any UI not updating in a
 * good time, but using too short values will result in devices being removed
 * and re-added as different #DfuDevice's.
 *
 * Since: 0.5.4
 **/
void
dfu_context_set_timeout (DfuContext *context, guint timeout)
{
	DfuContextPrivate *priv = GET_PRIVATE (context);
	g_return_if_fail (DFU_IS_CONTEXT (context));
	priv->timeout = timeout;
}


/**
 * dfu_context_enumerate:
 * @context: a #DfuContext
 * @error: a #GError, or %NULL
 *
 * Opens a DFU-capable context.
 *
 * Return value: %TRUE for success
 *
 * Since: 0.5.4
 **/
gboolean
dfu_context_enumerate (DfuContext *context, GError **error)
{
	DfuContextPrivate *priv = GET_PRIVATE (context);
	g_return_val_if_fail (DFU_IS_CONTEXT (context), FALSE);
	g_return_val_if_fail (error == NULL || *error == NULL, FALSE);
	g_usb_context_enumerate (priv->usb_ctx);
	return TRUE;
}

/**
 * dfu_context_get_devices:
 * @context: a #DfuContext
 *
 * Gets all the DFU-capable devices on the system.
 *
 * Return value: (element-type DfuDevice) (transfer container): array of devices
 *
 * Since: 0.5.4
 **/
GPtrArray *
dfu_context_get_devices (DfuContext *context)
{
	DfuContextPrivate *priv = GET_PRIVATE (context);
	DfuContextItem *item;
	GPtrArray *devices;
	guint i;

	g_return_val_if_fail (DFU_IS_CONTEXT (context), NULL);

	devices = g_ptr_array_new_with_free_func ((GDestroyNotify) g_object_unref);
	for (i = 0; i < priv->devices->len; i++) {
		item = g_ptr_array_index (priv->devices, i);
		g_ptr_array_add (devices, g_object_ref (item->device));
	}
	return devices;
}

/**
 * dfu_context_get_device_by_vid_pid:
 * @context: a #DfuContext
 * @vid: a vendor ID
 * @pid: a product ID
 * @error: a #GError, or %NULL
 *
 * Finds a device in the context with a specific vendor:product ID.
 * An error is returned if more than one device matches.
 *
 * Return value: (transfer full): a #DfuDevice for success, or %NULL for an error
 *
 * Since: 0.5.4
 **/
DfuDevice *
dfu_context_get_device_by_vid_pid (DfuContext *context,
				   guint16 vid, guint16 pid,
				   GError **error)
{
	DfuContextPrivate *priv = GET_PRIVATE (context);
	DfuContextItem *item;
	DfuDevice *device = NULL;
	GUsbDevice *dev;
	guint i;

	g_return_val_if_fail (DFU_IS_CONTEXT (context), NULL);
	g_return_val_if_fail (error == NULL || *error == NULL, NULL);

	/* search all devices */
	for (i = 0; i < priv->devices->len; i++) {

		/* match */
		item = g_ptr_array_index (priv->devices, i);
		dev = dfu_device_get_usb_dev (item->device);
		if (g_usb_device_get_vid (dev) == vid &&
		    g_usb_device_get_pid (dev) == pid) {
			if (device != NULL) {
				g_set_error (error,
					     DFU_ERROR,
					     DFU_ERROR_INVALID_DEVICE,
					     "multiple device matches for %04x:%04x",
					     vid, pid);
				return NULL;
			}
			device = item->device;
			continue;
		}
	}
	if (device == NULL) {
		g_set_error (error,
			     DFU_ERROR,
			     DFU_ERROR_NOT_FOUND,
			     "no device matches for %04x:%04x",
			     vid, pid);
		return NULL;
	}
	return g_object_ref (device);
}

/**
 * dfu_context_get_device_by_platform_id:
 * @context: a #DfuContext
 * @platform_id: a platform ID
 * @error: a #GError, or %NULL
 *
 * Finds a device in the context with a specific platform ID.
 *
 * Return value: (transfer full): a #DfuDevice for success, or %NULL for an error
 *
 * Since: 0.5.4
 **/
DfuDevice *
dfu_context_get_device_by_platform_id (DfuContext *context,
				       const gchar *platform_id,
				       GError **error)
{
	DfuContextPrivate *priv = GET_PRIVATE (context);
	DfuContextItem *item;
	guint i;

	g_return_val_if_fail (DFU_IS_CONTEXT (context), NULL);
	g_return_val_if_fail (error == NULL || *error == NULL, NULL);

	/* search all devices */
	for (i = 0; i < priv->devices->len; i++) {
		item = g_ptr_array_index (priv->devices, i);
		if (g_strcmp0 (dfu_device_get_platform_id (item->device),
			       platform_id) == 0) {
			return g_object_ref (item->device);
		}
	}
	g_set_error (error,
		     DFU_ERROR,
		     DFU_ERROR_NOT_FOUND,
		     "no device matches for %s",
		     platform_id);
	return NULL;
}

/**
 * dfu_context_get_device_default:
 * @context: a #DfuContext
 * @error: a #GError, or %NULL
 *
 * Gets the default device in the context.
 * An error is returned if more than one device exists.
 *
 * Return value: (transfer full): a #DfuDevice for success, or %NULL for an error
 *
 * Since: 0.5.4
 **/
DfuDevice *
dfu_context_get_device_default (DfuContext *context, GError **error)
{
	DfuContextPrivate *priv = GET_PRIVATE (context);
	DfuContextItem *item;

	g_return_val_if_fail (DFU_IS_CONTEXT (context), NULL);
	g_return_val_if_fail (error == NULL || *error == NULL, NULL);

	/* none */
	if (priv->devices->len == 0) {
		g_set_error_literal (error,
				     DFU_ERROR,
				     DFU_ERROR_NOT_FOUND,
				     "no attached DFU device");
		return NULL;
	}

	/* multiple */
	if (priv->devices->len > 1) {
		g_set_error_literal (error,
				     DFU_ERROR,
				     DFU_ERROR_INVALID_DEVICE,
				     "more than one attached DFU device");
		return NULL;
	}
	item = g_ptr_array_index (priv->devices, 0);
	return g_object_ref (item->device);
}
