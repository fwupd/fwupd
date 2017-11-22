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
 * has been changed. If the #FuDevice has changed during a device replug then
 * the ::changed signal will be emitted instead of ::added and then ::removed.
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

typedef struct {
	FuDevice		*device;
	FuDevice		*device_old;
	FuDeviceList		*self;		/* no ref */
	guint			 remove_id;
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

static void
fu_device_list_emit_device_changed (FuDeviceList *self, FuDevice *device)
{
	g_debug ("::changed %s", fu_device_get_id (device));
	g_signal_emit (self, signals[SIGNAL_CHANGED], 0, device);
}

/**
 * fu_device_list_get_all:
 * @self: A #FuDeviceList
 *
 * Returns all the devices that have been added to the device list.
 * This includes devices that are no longer active, for instance where a
 * different plugin has taken over responsibility of the #FuDevice.
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
	for (guint i = 0; i < self->devices->len; i++) {
		FuDeviceItem *item = g_ptr_array_index (self->devices, i);
		if (item->device_old == NULL)
			continue;
		g_ptr_array_add (devices, g_object_ref (item->device_old));
	}
	return devices;
}

/**
 * fu_device_list_get_active:
 * @self: A #FuDeviceList
 *
 * Returns all the active devices that have been added to the device list.
 * An active device is defined as a device that is currently conected and has
 * is owned by a plugin.
 *
 * Returns: (transfer container) (element-type FuDevice): the devices
 *
 * Since: 1.0.2
 **/
GPtrArray *
fu_device_list_get_active (FuDeviceList *self)
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

static FuDeviceItem *
fu_device_list_find_by_device (FuDeviceList *self, FuDevice *device)
{
	for (guint i = 0; i < self->devices->len; i++) {
		FuDeviceItem *item = g_ptr_array_index (self->devices, i);
		if (item->device == device)
			return item;
	}
	for (guint i = 0; i < self->devices->len; i++) {
		FuDeviceItem *item = g_ptr_array_index (self->devices, i);
		if (item->device_old == device)
			return item;
	}
	return NULL;
}

static FuDeviceItem *
fu_device_list_find_by_guids (FuDeviceList *self, GPtrArray *guids)
{
	for (guint i = 0; i < self->devices->len; i++) {
		FuDeviceItem *item = g_ptr_array_index (self->devices, i);
		for (guint j = 0; j < guids->len; j++) {
			const gchar *guid = g_ptr_array_index (guids, j);
			if (fu_device_has_guid (item->device, guid))
				return item;
		}
	}
	for (guint i = 0; i < self->devices->len; i++) {
		FuDeviceItem *item = g_ptr_array_index (self->devices, i);
		if (item->device_old == NULL)
			continue;
		for (guint j = 0; j < guids->len; j++) {
			const gchar *guid = g_ptr_array_index (guids, j);
			if (fu_device_has_guid (item->device_old, guid))
				return item;
		}
	}
	return NULL;
}

static gboolean
fu_device_list_device_delayed_remove_cb (gpointer user_data)
{
	FuDeviceItem *item = (FuDeviceItem *) user_data;
	FuDeviceList *self = FU_DEVICE_LIST (item->self);

	/* no longer valid */
	item->remove_id = 0;

	/* just remove now */
	g_debug ("doing delayed removal");
	fu_device_list_emit_device_removed (self, item->device);
	g_ptr_array_remove (self->devices, item);
	return G_SOURCE_REMOVE;
}

/**
 * fu_device_list_remove:
 * @self: A #FuDeviceList
 * @device: A #FuDevice
 *
 * Removes a specific device from the list if it exists.
 *
 * If the @device has a remove-delay set then a timeout will be started. If
 * the exact same #FuDevice is added to the list with fu_device_list_add()
 * within the timeout then only a ::changed signal will be emitted.
 *
 * If there is no remove-delay set, the ::removed signal will be emitted
 * straight away.
 *
 * Since: 1.0.2
 **/
void
fu_device_list_remove (FuDeviceList *self, FuDevice *device)
{
	FuDeviceItem *item;

	g_return_if_fail (FU_IS_DEVICE_LIST (self));
	g_return_if_fail (FU_IS_DEVICE (device));

	/* check the device already exists */
	item = fu_device_list_find_by_device (self, device);
	if (item == NULL) {
		g_debug ("device %s not found", fu_device_get_id (device));
		return;
	}

	/* ensure never fired if the remove delay is changed */
	if (item->remove_id > 0) {
		g_source_remove (item->remove_id);
		item->remove_id = 0;
	}

	/* delay the removal and check for replug */
	if (fu_device_get_remove_delay (item->device) > 0) {

		/* we can't do anything with an unconnected device */
		fu_device_set_flags (item->device, FWUPD_DEVICE_FLAG_NONE);

		/* give the hardware time to re-enumerate or the user time to
		 * re-insert the device with a magic button pressed */
		g_debug ("waiting %ums for device removal",
			 fu_device_get_remove_delay (item->device));
		item->remove_id = g_timeout_add (fu_device_get_remove_delay (item->device),
						 fu_device_list_device_delayed_remove_cb,
						 item);
		return;
	}

	/* remove right now */
	fu_device_list_emit_device_removed (self, item->device);
	g_ptr_array_remove (self->devices, item);
}

/**
 * fu_device_list_add:
 * @self: A #FuDeviceList
 * @device: A #FuDevice
 * @error: A #GError, or %NULL
 *
 * Adds a specific device to the device list if not already present.
 *
 * If the @device (or a compatible @device) has been previously removed within
 * the remove-timeout then only the ::changed signal will be emitted on calling
 * this function. Otherwise the ::added signal will be emitted straight away.
 *
 * Compatible devices are defined as #FuDevice objects that share at least one
 * device GUID. If a compatible device is matched then the vendor ID and
 * version will be copied to the new object if they are not already set.
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

	/* verify the device does not already exist */
	item = fu_device_list_find_by_device (self, device);
	if (item != NULL) {
		g_debug ("found existing device %s, reusing item",
			 fu_device_get_id (item->device));
		if (item->remove_id != 0) {
			g_source_remove (item->remove_id);
			item->remove_id = 0;
		}
		fu_device_list_emit_device_changed (self, device);
		return;
	}

	/* verify a compatible device does not already exist */
	item = fu_device_list_find_by_guids (self, fu_device_get_guids (device));
	if (item != NULL) {
		g_debug ("found compatible device %s, reusing item "
			 "from plugin %s for plugin %s",
			 fu_device_get_id (item->device),
			 fu_device_get_plugin (item->device),
			 fu_device_get_plugin (device));
		if (item->remove_id != 0) {
			g_source_remove (item->remove_id);
			item->remove_id = 0;
		}

		/* enforce the vendor ID if specified */
		if (fu_device_get_vendor_id (item->device) != NULL &&
		    fu_device_get_vendor_id (device) == NULL) {
			const gchar *vendor_id = fu_device_get_vendor_id (item->device);
			g_debug ("copying old vendor ID %s to new device", vendor_id);
			fu_device_set_vendor_id (device, vendor_id);
		}

		/* copy over the version strings if not set */
		if (fu_device_get_version (item->device) != NULL &&
		    fu_device_get_version (device) == NULL) {
			const gchar *version = fu_device_get_version (item->device);
			g_debug ("copying old version %s to new device", version);
			fu_device_set_version (device, version);
		}

		/* assign the new device */
		g_set_object (&item->device_old, item->device);
		g_set_object (&item->device, device);
		fu_device_list_emit_device_changed (self, device);
		return;
	}

	/* add helper */
	item = g_new0 (FuDeviceItem, 1);
	item->self = self; /* no ref */
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
	for (guint i = 0; i < self->devices->len; i++) {
		FuDeviceItem *item = g_ptr_array_index (self->devices, i);
		if (item->device_old == NULL)
			continue;
		if (fu_device_has_guid (item->device_old, guid))
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
	if (item->remove_id != 0)
		g_source_remove (item->remove_id);
	if (item->device_old != NULL)
		g_object_unref (item->device_old);
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
