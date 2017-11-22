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

#include <glib-object.h>
#include <string.h>

#include "fu-device-list.h"

#include "fwupd-error.h"

/**
 * SECTION:fu-device-list
 * @short_description: a list of devices
 *
 * This list of devices provides a way to find a device using either the
 * device-id or a GUID.
 *
 * The device list will emit ::added and ::removed signals when the device list
 * has been changed.
 *
 * See also: #FuDevice
 */

static void fu_device_list_finalize	 (GObject *obj);

struct _FuDeviceList
{
	GObject			 parent_instance;
	GPtrArray		*devices;	/* of FuDeviceItem */
};

enum {
	SIGNAL_ADDED,
	SIGNAL_REMOVED,
	SIGNAL_CHANGED,
	SIGNAL_LAST
};

static guint signals[SIGNAL_LAST] = { 0 };

/* although this seems a waste of time, there are great plans for this... */
typedef struct {
	FuDevice		*device;
} FuDeviceItem;

G_DEFINE_TYPE (FuDeviceList, fu_device_list, G_TYPE_OBJECT)

static void
fu_device_list_emit_device_added (FuDeviceList *self, FuDevice *device)
{
	g_debug ("::added %s", fu_device_get_id (device));
	g_signal_emit (self, signals[SIGNAL_ADDED], 0, device);
}

static void
fu_device_list_emit_device_removed (FuDeviceList *self, FuDevice *device)
{
	g_debug ("::removed %s", fu_device_get_id (device));
	g_signal_emit (self, signals[SIGNAL_REMOVED], 0, device);
}

/**
 * fu_device_list_get_all:
 * @self: A #FuDeviceList
 *
 * Returns all the devices that have been added to the device list.
 *
 * Returns: (transfer container) (element-type FuDevice): the devices
 *
 * Since: 1.0.2
 **/
GPtrArray *
fu_device_list_get_all (FuDeviceList *self)
{
	GPtrArray *devices;
	g_return_val_if_fail (FU_IS_DEVICE_LIST (self), NULL);
	devices = g_ptr_array_new_with_free_func ((GDestroyNotify) g_object_unref);
	for (guint i = 0; i < self->devices->len; i++) {
		FuDeviceItem *item = g_ptr_array_index (self->devices, i);
		g_ptr_array_add (devices, g_object_ref (item->device));
	}
	return devices;
}

/**
 * fu_device_list_remove:
 * @self: A #FuDeviceList
 * @device: A #FuDevice
 *
 * Removes a specific device from the list if it exists.
 *
 * The ::removed signal will also be emitted if @device is found in the list.
 *
 * Since: 1.0.2
 **/
void
fu_device_list_remove (FuDeviceList *self, FuDevice *device)
{
	g_return_if_fail (FU_IS_DEVICE_LIST (self));
	g_return_if_fail (FU_IS_DEVICE (device));

	for (guint i = 0; i < self->devices->len; i++) {
		FuDeviceItem *item = g_ptr_array_index (self->devices, i);
		if (item->device == device) {
			g_ptr_array_remove (self->devices, item);
			fu_device_list_emit_device_removed (self, device);
			return;
		}
	}
}

/**
 * fu_device_list_add:
 * @self: A #FuDeviceList
 * @device: A #FuDevice
 * @error: A #GError, or %NULL
 *
 * Adds a specific device to the device list.
 *
 * The ::added signal will also be emitted if @device is not already found in
 * the list.
 *
 * Returns: (transfer none): a device, or %NULL if not found
 *
 * Since: 1.0.2
 **/
void
fu_device_list_add (FuDeviceList *self, FuDevice *device)
{
	FuDeviceItem *item;

	g_return_if_fail (FU_IS_DEVICE_LIST (self));
	g_return_if_fail (FU_IS_DEVICE (device));

	/* FIXME: verify the device does not already exist */

	/* add helper */
	item = g_new0 (FuDeviceItem, 1);
	item->device = g_object_ref (device);
	g_ptr_array_add (self->devices, item);
	fu_device_list_emit_device_added (self, device);
}

/**
 * fu_device_list_find_by_guid:
 * @self: A #FuDeviceList
 * @guid: A device GUID
 * @error: A #GError, or %NULL
 *
 * Finds a specific device that has the matching GUID.
 *
 * Returns: (transfer none): a device, or %NULL if not found
 *
 * Since: 1.0.2
 **/
FuDevice *
fu_device_list_find_by_guid (FuDeviceList *self, const gchar *guid, GError **error)
{
	g_return_val_if_fail (FU_IS_DEVICE_LIST (self), NULL);
	g_return_val_if_fail (guid != NULL, NULL);
	g_return_val_if_fail (error == NULL || *error == NULL, NULL);
	for (guint i = 0; i < self->devices->len; i++) {
		FuDeviceItem *item = g_ptr_array_index (self->devices, i);
		if (fu_device_has_guid (item->device, guid))
			return item->device;
	}
	g_set_error (error,
		     FWUPD_ERROR,
		     FWUPD_ERROR_NOT_FOUND,
		     "GUID %s was not found",
		     guid);
	return NULL;
}

/**
 * fu_device_list_find_by_id:
 * @self: A #FuDeviceList
 * @device_id: A device ID, typically a SHA1 hash
 * @error: A #GError, or %NULL
 *
 * Finds a specific device using the ID string. This function also supports
 * using abbreviated hashes.
 *
 * Returns: (transfer none): a device, or %NULL if not found
 *
 * Since: 1.0.2
 **/
FuDevice *
fu_device_list_find_by_id (FuDeviceList *self, const gchar *device_id, GError **error)
{
	FuDeviceItem *item = NULL;
	gboolean multiple_matches = FALSE;
	gsize device_id_len;

	g_return_val_if_fail (FU_IS_DEVICE_LIST (self), NULL);
	g_return_val_if_fail (device_id != NULL, NULL);
	g_return_val_if_fail (error == NULL || *error == NULL, NULL);

	/* support abbreviated hashes */
	device_id_len = strlen (device_id);
	for (gsize i = 0; i < self->devices->len; i++) {
		FuDeviceItem *item_tmp = g_ptr_array_index (self->devices, i);
		const gchar *ids[] = {
			fu_device_get_id (item_tmp->device),
			fu_device_get_equivalent_id (item_tmp->device),
			NULL };
		for (guint j = 0; ids[j] != NULL; j++) {
			if (strncmp (ids[j], device_id, device_id_len) == 0) {
				if (item != NULL)
					multiple_matches = TRUE;
				item = item_tmp;
			}
		}
	}

	/* nothing at all matched */
	if (item == NULL) {
		g_set_error (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_NOT_FOUND,
			     "device ID %s was not found",
			     device_id);
		return NULL;
	}

	/* multiple things matched */
	if (multiple_matches) {
		g_set_error (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_NOT_SUPPORTED,
			     "device ID %s was not unique",
			     device_id);
		return NULL;
	}

	/* something found */
	return item->device;
}

static void
fu_device_list_item_free (FuDeviceItem *item)
{
	g_object_unref (item->device);
	g_free (item);
}

static void
fu_device_list_class_init (FuDeviceListClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	object_class->finalize = fu_device_list_finalize;

	signals[SIGNAL_ADDED] =
		g_signal_new ("added",
			      G_TYPE_FROM_CLASS (object_class), G_SIGNAL_RUN_LAST,
			      0, NULL, NULL, g_cclosure_marshal_VOID__OBJECT,
			      G_TYPE_NONE, 1, FU_TYPE_DEVICE);
	signals[SIGNAL_REMOVED] =
		g_signal_new ("removed",
			      G_TYPE_FROM_CLASS (object_class), G_SIGNAL_RUN_LAST,
			      0, NULL, NULL, g_cclosure_marshal_VOID__OBJECT,
			      G_TYPE_NONE, 1, FU_TYPE_DEVICE);
	signals[SIGNAL_CHANGED] =
		g_signal_new ("changed",
			      G_TYPE_FROM_CLASS (object_class), G_SIGNAL_RUN_LAST,
			      0, NULL, NULL, g_cclosure_marshal_VOID__OBJECT,
			      G_TYPE_NONE, 1, FU_TYPE_DEVICE);
}

static void
fu_device_list_init (FuDeviceList *self)
{
	self->devices = g_ptr_array_new_with_free_func ((GDestroyNotify) fu_device_list_item_free);
}

static void
fu_device_list_finalize (GObject *obj)
{
	FuDeviceList *self = FU_DEVICE_LIST (obj);

	g_ptr_array_unref (self->devices);

	G_OBJECT_CLASS (fu_device_list_parent_class)->finalize (obj);
}

/**
 * fu_device_list_new:
 *
 * Creates a new device list.
 *
 * Returns: (transfer full): a #FuDeviceList
 *
 * Since: 1.0.2
 **/
FuDeviceList *
fu_device_list_new (void)
{
	FuDeviceList *self;
	self = g_object_new (FU_TYPE_DEVICE_LIST, NULL);
	return FU_DEVICE_LIST (self);
}
