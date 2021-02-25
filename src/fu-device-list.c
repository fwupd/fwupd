/*
 * Copyright (C) 2017 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#define G_LOG_DOMAIN				"FuDeviceList"

#include "config.h"

#include <glib-object.h>
#include <string.h>

#include "fu-device-list.h"
#include "fu-device-private.h"
#include "fu-mutex.h"

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
	GRWLock			 devices_mutex;
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

/* we cannot use fu_device_get_children() as this will not find "parent-only"
 * logical relationships added using fu_device_add_parent_guid() */
static GPtrArray *
fu_device_list_get_children (FuDeviceList *self, FuDevice *device)
{
	GPtrArray *devices = g_ptr_array_new_with_free_func ((GDestroyNotify) g_object_unref);
	g_rw_lock_reader_lock (&self->devices_mutex);
	for (guint i = 0; i < self->devices->len; i++) {
		FuDeviceItem *item = g_ptr_array_index (self->devices, i);
		if (device == fu_device_get_parent (item->device))
			g_ptr_array_add (devices, g_object_ref (item->device));
	}
	g_rw_lock_reader_unlock (&self->devices_mutex);
	return devices;
}

static void
fu_device_list_depsolve_order_full (FuDeviceList *self, FuDevice *device, guint depth)
{
	g_autoptr(GPtrArray) children = NULL;

	/* ourself */
	fu_device_set_order (device, depth);

	/* optional children */
	children = fu_device_list_get_children (self, device);
	for (guint i = 0; i < children->len; i++) {
		FuDevice *child = g_ptr_array_index (children, i);
		if (fu_device_has_flag (child, FWUPD_DEVICE_FLAG_INSTALL_PARENT_FIRST)) {
			fu_device_list_depsolve_order_full (self, child, depth + 1);
		} else {
			fu_device_list_depsolve_order_full (self, child, depth - 1);
		}
	}
}

/**
 * fu_device_list_depsolve_order:
 * @self: A #FuDeviceList
 * @device: A #FuDevice
 *
 * Sets the device order using the logical parent->child relationships -- by default
 * the child is updated first, unless the device has set flag
 * %FWUPD_DEVICE_FLAG_INSTALL_PARENT_FIRST.
 *
 * Since: 1.5.0
 **/
void
fu_device_list_depsolve_order (FuDeviceList *self, FuDevice *device)
{
	g_autoptr(FuDevice) root = fu_device_get_root (device);
	fu_device_list_depsolve_order_full (self, root, 0);
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

	g_rw_lock_reader_lock (&self->devices_mutex);
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
	g_rw_lock_reader_unlock (&self->devices_mutex);
	return devices;
}

/**
 * fu_device_list_get_active:
 * @self: A #FuDeviceList
 *
 * Returns all the active devices that have been added to the device list.
 * An active device is defined as a device that is currently connected and has
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
	g_rw_lock_reader_lock (&self->devices_mutex);
	for (guint i = 0; i < self->devices->len; i++) {
		FuDeviceItem *item = g_ptr_array_index (self->devices, i);
		g_ptr_array_add (devices, g_object_ref (item->device));
	}
	g_rw_lock_reader_unlock (&self->devices_mutex);
	return devices;
}

static FuDeviceItem *
fu_device_list_find_by_device (FuDeviceList *self, FuDevice *device)
{
	g_autoptr(GRWLockReaderLocker) locker = g_rw_lock_reader_locker_new (&self->devices_mutex);
	g_return_val_if_fail (locker != NULL, NULL);
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
fu_device_list_find_by_guid (FuDeviceList *self, const gchar *guid)
{
	g_autoptr(GRWLockReaderLocker) locker = g_rw_lock_reader_locker_new (&self->devices_mutex);
	g_return_val_if_fail (locker != NULL, NULL);
	for (guint i = 0; i < self->devices->len; i++) {
		FuDeviceItem *item = g_ptr_array_index (self->devices, i);
		if (fu_device_has_guid (item->device, guid))
			return item;
	}
	for (guint i = 0; i < self->devices->len; i++) {
		FuDeviceItem *item = g_ptr_array_index (self->devices, i);
		if (item->device_old == NULL)
			continue;
		if (fu_device_has_guid (item->device_old, guid))
			return item;
	}
	return NULL;
}

static FuDeviceItem *
fu_device_list_find_by_connection (FuDeviceList *self,
				   const gchar *physical_id,
				   const gchar *logical_id)
{
	g_autoptr(GRWLockReaderLocker) locker = NULL;
	if (physical_id == NULL)
		return NULL;
	locker = g_rw_lock_reader_locker_new (&self->devices_mutex);
	g_return_val_if_fail (locker != NULL, NULL);
	for (guint i = 0; i < self->devices->len; i++) {
		FuDeviceItem *item_tmp = g_ptr_array_index (self->devices, i);
		FuDevice *device = item_tmp->device;
		if (device != NULL &&
		    g_strcmp0 (fu_device_get_physical_id (device), physical_id) == 0 &&
		    g_strcmp0 (fu_device_get_logical_id (device), logical_id) == 0)
			return item_tmp;
	}
	for (guint i = 0; i < self->devices->len; i++) {
		FuDeviceItem *item_tmp = g_ptr_array_index (self->devices, i);
		FuDevice *device = item_tmp->device_old;
		if (device != NULL &&
		    g_strcmp0 (fu_device_get_physical_id (device), physical_id) == 0 &&
		    g_strcmp0 (fu_device_get_logical_id (device), logical_id) == 0)
			return item_tmp;
	}
	return NULL;
}

static FuDeviceItem *
fu_device_list_find_by_id (FuDeviceList *self,
			   const gchar *device_id,
			   gboolean *multiple_matches)
{
	FuDeviceItem *item = NULL;
	gsize device_id_len;

	/* sanity check */
	if (device_id == NULL) {
		g_critical ("device ID was NULL");
		return NULL;
	}

	/* support abbreviated hashes */
	device_id_len = strlen (device_id);
	g_rw_lock_reader_lock (&self->devices_mutex);
	for (guint i = 0; i < self->devices->len; i++) {
		FuDeviceItem *item_tmp = g_ptr_array_index (self->devices, i);
		const gchar *ids[] = {
			fu_device_get_id (item_tmp->device),
			fu_device_get_equivalent_id (item_tmp->device),
			NULL };
		for (guint j = 0; ids[j] != NULL; j++) {
			if (strncmp (ids[j], device_id, device_id_len) == 0) {
				if (item != NULL && multiple_matches != NULL)
					*multiple_matches = TRUE;
				item = item_tmp;
			}
		}
	}
	g_rw_lock_reader_unlock (&self->devices_mutex);
	if (item != NULL)
		return item;

	/* only search old devices if we didn't find the active device */
	g_rw_lock_reader_lock (&self->devices_mutex);
	for (guint i = 0; i < self->devices->len; i++) {
		FuDeviceItem *item_tmp = g_ptr_array_index (self->devices, i);
		const gchar *ids[3] = { NULL };
		if (item_tmp->device_old == NULL)
			continue;
		ids[0] = fu_device_get_id (item_tmp->device_old);
		ids[1] = fu_device_get_equivalent_id (item_tmp->device_old);
		for (guint j = 0; ids[j] != NULL; j++) {
			if (strncmp (ids[j], device_id, device_id_len) == 0) {
				if (item != NULL && multiple_matches != NULL)
					*multiple_matches = TRUE;
				item = item_tmp;
			}
		}
	}
	g_rw_lock_reader_unlock (&self->devices_mutex);
	return item;
}

/**
 * fu_device_list_get_old:
 * @self: A #FuDeviceList
 * @device: A #FuDevice
 *
 * Returns the old device associated with the currently active device.
 *
 * Returns: (transfer full): the device, or %NULL if not found
 *
 * Since: 1.0.3
 **/
FuDevice *
fu_device_list_get_old (FuDeviceList *self, FuDevice *device)
{
	FuDeviceItem *item = fu_device_list_find_by_device (self, device);
	if (item == NULL)
		return NULL;
	if (item->device_old == NULL)
		return NULL;
	return g_object_ref (item->device_old);
}

static FuDeviceItem *
fu_device_list_get_by_guids_removed (FuDeviceList *self, GPtrArray *guids)
{
	g_autoptr(GRWLockReaderLocker) locker = g_rw_lock_reader_locker_new (&self->devices_mutex);
	g_return_val_if_fail (locker != NULL, NULL);
	for (guint i = 0; i < self->devices->len; i++) {
		FuDeviceItem *item = g_ptr_array_index (self->devices, i);
		if (item->remove_id == 0)
			continue;
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
		if (item->remove_id == 0)
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
	GPtrArray *children;

	/* no longer valid */
	item->remove_id = 0;

	/* remove any children associated with device */
	children = fu_device_get_children (item->device);
	for (guint j = 0; j < children->len; j++) {
		FuDevice *child = g_ptr_array_index (children, j);
		FuDeviceItem *child_item = fu_device_list_find_by_id (self,
								      fu_device_get_id (child),
								      NULL);
		if (child_item == NULL) {
			g_debug ("device %s not found", fu_device_get_id (child));
			continue;
		}
		fu_device_list_emit_device_removed (self, child);
		g_rw_lock_writer_lock (&self->devices_mutex);
		g_ptr_array_remove (self->devices, child_item);
		g_rw_lock_writer_unlock (&self->devices_mutex);
	}

	/* just remove now */
	g_debug ("doing delayed removal");
	fu_device_list_emit_device_removed (self, item->device);
	g_rw_lock_writer_lock (&self->devices_mutex);
	g_ptr_array_remove (self->devices, item);
	g_rw_lock_writer_unlock (&self->devices_mutex);
	return G_SOURCE_REMOVE;
}

static void
fu_device_list_remove_with_delay (FuDeviceItem *item)
{
	/* we can't do anything with an unconnected device */
	fu_device_remove_flag (item->device, FWUPD_DEVICE_FLAG_UPDATABLE);

	/* give the hardware time to re-enumerate or the user time to
	 * re-insert the device with a magic button pressed */
	g_debug ("waiting %ums for %s device removal",
		 fu_device_get_remove_delay (item->device),
		 fu_device_get_name (item->device));
	item->remove_id = g_timeout_add (fu_device_get_remove_delay (item->device),
					 fu_device_list_device_delayed_remove_cb,
					 item);
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
	GPtrArray *children;

	g_return_if_fail (FU_IS_DEVICE_LIST (self));
	g_return_if_fail (FU_IS_DEVICE (device));

	/* check the device already exists */
	item = fu_device_list_find_by_id (self, fu_device_get_id (device), NULL);
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
		fu_device_list_remove_with_delay (item);
		return;
	}

	/* remove any children associated with device */
	children = fu_device_get_children (device);
	for (guint j = 0; j < children->len; j++) {
		FuDevice *child = g_ptr_array_index (children, j);
		FuDeviceItem *child_item = fu_device_list_find_by_id (self,
								      fu_device_get_id (child),
								      NULL);
		if (child_item == NULL) {
			g_debug ("device %s not found", fu_device_get_id (child));
			continue;
		}
		fu_device_list_emit_device_removed (self, child);
		g_rw_lock_writer_lock (&self->devices_mutex);
		g_ptr_array_remove (self->devices, child_item);
		g_rw_lock_writer_unlock (&self->devices_mutex);
	}

	/* remove right now */
	fu_device_list_emit_device_removed (self, item->device);
	g_rw_lock_writer_lock (&self->devices_mutex);
	g_ptr_array_remove (self->devices, item);
	g_rw_lock_writer_unlock (&self->devices_mutex);
}

static void
fu_device_list_add_missing_guids (FuDevice *device_new, FuDevice *device_old)
{
	GPtrArray *guids_old = fu_device_get_guids (device_old);
	for (guint i = 0; i < guids_old->len; i++) {
		const gchar *guid_tmp = g_ptr_array_index (guids_old, i);
		if (!fu_device_has_guid (device_new, guid_tmp)) {
			if (fu_device_has_flag (device_new, FWUPD_DEVICE_FLAG_ADD_COUNTERPART_GUIDS)) {
				g_debug ("adding GUID %s to device", guid_tmp);
				fu_device_add_counterpart_guid (device_new, guid_tmp);
			} else {
				g_debug ("not adding GUID %s to device, use "
					 "FWUPD_DEVICE_FLAG_ADD_COUNTERPART_GUIDS if required",
					 guid_tmp);
			}
		}
	}
}

static void
fu_device_list_item_finalized_cb (gpointer data, GObject *where_the_object_was)
{
	FuDeviceItem *item = (FuDeviceItem *) data;
	FuDeviceList *self = FU_DEVICE_LIST (item->self);

	g_critical ("FuDevice %p was finalized without being removed from "
		    "FuDeviceList, removing item!",
		    where_the_object_was);
	g_rw_lock_writer_lock (&self->devices_mutex);
	g_ptr_array_remove (self->devices, item);
	g_rw_lock_writer_unlock (&self->devices_mutex);
}

/* this should never be required, and yet here we are */
static void
fu_device_list_item_set_device (FuDeviceItem *item, FuDevice *device)
{
	if (item->device != NULL) {
		g_object_weak_unref (G_OBJECT (item->device),
				     fu_device_list_item_finalized_cb,
				     item);
	}
	if (device != NULL) {
		g_object_weak_ref (G_OBJECT (device),
				   fu_device_list_item_finalized_cb,
				   item);
	}
	g_set_object (&item->device, device);
}

static void
fu_device_list_replace (FuDeviceList *self, FuDeviceItem *item, FuDevice *device)
{
	const gchar *custom_flags;
	GPtrArray *vendor_ids;

	/* clear timeout if scheduled */
	if (item->remove_id != 0) {
		g_source_remove (item->remove_id);
		item->remove_id = 0;
	}

	/* copy over any GUIDs that used to exist */
	fu_device_list_add_missing_guids (device, item->device);

	/* enforce the vendor ID if specified */
	vendor_ids = fu_device_get_vendor_ids (item->device);
	for (guint i = 0; i < vendor_ids->len; i++) {
		const gchar *vendor_id = g_ptr_array_index (vendor_ids, i);
		g_debug ("copying old vendor ID %s to new device", vendor_id);
		fu_device_add_vendor_id (device, vendor_id);
	}

	/* copy over custom flags */
	custom_flags = fu_device_get_custom_flags (item->device);
	if (custom_flags != NULL) {
		g_debug ("copying old custom flags %s to new device", custom_flags);
		fu_device_set_custom_flags (device, custom_flags);
	}

	/* copy over the version strings if not set */
	if (fu_device_get_version (item->device) != NULL &&
	    fu_device_get_version (device) == NULL) {
		const gchar *version = fu_device_get_version (item->device);
		g_debug ("copying old version %s to new device", version);
		fu_device_set_version_format (device, fu_device_get_version_format (item->device));
		fu_device_set_version (device, version);
	}

	/* always use the runtime version */
	if (fu_device_has_flag (item->device, FWUPD_DEVICE_FLAG_USE_RUNTIME_VERSION) &&
	    fu_device_has_flag (item->device, FWUPD_DEVICE_FLAG_NEEDS_BOOTLOADER)) {
		const gchar *version = fu_device_get_version (item->device);
		g_debug ("forcing runtime version %s to new device", version);
		fu_device_set_version_format (device, fu_device_get_version_format (item->device));
		fu_device_set_version (device, version);
	}

	/* allow another plugin to handle the write too */
	if (fu_device_has_flag (item->device, FWUPD_DEVICE_FLAG_ANOTHER_WRITE_REQUIRED)) {
		g_debug ("copying another-write-required to new device");
		fu_device_add_flag (device, FWUPD_DEVICE_FLAG_ANOTHER_WRITE_REQUIRED);
	}

	/* device won't come back in right mode */
	if (fu_device_has_flag (item->device, FWUPD_DEVICE_FLAG_WILL_DISAPPEAR)) {
		g_debug ("copying will-disappear to new device");
		fu_device_add_flag (device, FWUPD_DEVICE_FLAG_WILL_DISAPPEAR);
	}

	/* copy the parent if not already set */
	if (fu_device_get_parent (item->device) != NULL &&
	    fu_device_get_parent (item->device) != device &&
	    fu_device_get_parent (device) != item->device &&
	    fu_device_get_parent (device) == NULL) {
		FuDevice *parent = fu_device_get_parent (item->device);
		g_debug ("copying parent %s to new device", fu_device_get_id (parent));
		fu_device_set_parent (device, parent);
	}

	/* assign the new device */
	g_set_object (&item->device_old, item->device);
	fu_device_list_item_set_device (item, device);
	fu_device_list_emit_device_changed (self, device);

	/* we were waiting for this... */
	if (fu_device_has_flag (item->device_old, FWUPD_DEVICE_FLAG_WAIT_FOR_REPLUG)) {
		g_debug ("device came back, clearing flag");
		fu_device_remove_flag (item->device_old, FWUPD_DEVICE_FLAG_WAIT_FOR_REPLUG);
	}
}

/**
 * fu_device_list_add:
 * @self: A #FuDeviceList
 * @device: A #FuDevice
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
 * Any GUIDs present on the old device and not on the new device will be
 * inherited and do not have to be copied over by plugins manually.
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

	/* is the device waiting to be replugged? */
	item = fu_device_list_find_by_id (self, fu_device_get_id (device), NULL);
	if (item != NULL) {
		g_debug ("found existing device %s, reusing item",
			 fu_device_get_id (item->device));
		fu_device_list_replace (self, item, device);
		return;
	}

	/* verify a device with same connection does not already exist */
	item = fu_device_list_find_by_connection (self,
						  fu_device_get_physical_id (device),
						  fu_device_get_logical_id (device));
	if (item != NULL && item->remove_id != 0) {
		g_debug ("found physical device %s recently removed, reusing "
			 "item from plugin %s for plugin %s",
			 fu_device_get_id (item->device),
			 fu_device_get_plugin (item->device),
			 fu_device_get_plugin (device));
		fu_device_list_replace (self, item, device);
		return;
	}

	/* verify a compatible device does not already exist */
	item = fu_device_list_get_by_guids_removed (self, fu_device_get_guids (device));
	if (item != NULL) {
		if (fu_device_has_internal_flag (device, FU_DEVICE_INTERNAL_FLAG_REPLUG_MATCH_GUID)) {
			g_debug ("found compatible device %s recently removed, reusing "
				 "item from plugin %s for plugin %s",
				 fu_device_get_id (item->device),
				 fu_device_get_plugin (item->device),
				 fu_device_get_plugin (device));
			fu_device_list_replace (self, item, device);
			return;
		} else {
			g_debug ("not adding matching %s for device add, use "
				 "FU_DEVICE_INTERNAL_FLAG_REPLUG_MATCH_GUID if required",
				 fu_device_get_id (item->device));
		}
	}

	/* add helper */
	item = g_new0 (FuDeviceItem, 1);
	item->self = self; /* no ref */
	fu_device_list_item_set_device (item, device);
	g_rw_lock_writer_lock (&self->devices_mutex);
	g_ptr_array_add (self->devices, item);
	g_rw_lock_writer_unlock (&self->devices_mutex);
	fu_device_list_emit_device_added (self, device);
}

/**
 * fu_device_list_get_by_guid:
 * @self: A #FuDeviceList
 * @guid: A device GUID
 * @error: A #GError, or %NULL
 *
 * Finds a specific device that has the matching GUID.
 *
 * Returns: (transfer full): a device, or %NULL if not found
 *
 * Since: 1.0.2
 **/
FuDevice *
fu_device_list_get_by_guid (FuDeviceList *self, const gchar *guid, GError **error)
{
	FuDeviceItem *item;
	g_return_val_if_fail (FU_IS_DEVICE_LIST (self), NULL);
	g_return_val_if_fail (guid != NULL, NULL);
	g_return_val_if_fail (error == NULL || *error == NULL, NULL);
	item = fu_device_list_find_by_guid (self, guid);
	if (item != NULL)
		return g_object_ref (item->device);
	g_set_error (error,
		     FWUPD_ERROR,
		     FWUPD_ERROR_NOT_FOUND,
		     "GUID %s was not found",
		     guid);
	return NULL;
}

/* count devices that are disconnected and are waiting to be replugged */
static guint
fu_device_list_devices_wait_removed (FuDeviceList *self)
{
	guint cnt = 0;
	g_rw_lock_reader_lock (&self->devices_mutex);
	for (guint i = 0; i < self->devices->len; i++) {
		FuDeviceItem *item = g_ptr_array_index (self->devices, i);
		if (item->remove_id != 0)
			cnt++;
	}
	g_rw_lock_reader_unlock (&self->devices_mutex);
	return cnt;
}

/**
 * fu_device_list_wait_for_replug:
 * @self: A #FuDeviceList
 * @device: A #FuDevice
 * @error: A #GError, or %NULL
 *
 * Waits for a specific device to replug if %FWUPD_DEVICE_FLAG_WAIT_FOR_REPLUG
 * is set.
 *
 * If the device does not exist this function returns without an error.
 *
 * Returns: %TRUE for success
 *
 * Since: 1.1.2
 **/
gboolean
fu_device_list_wait_for_replug (FuDeviceList *self, FuDevice *device, GError **error)
{
	FuDeviceItem *item;
	guint remove_delay;
	guint wait_removed;
	guint wait_removed_old = 0;
	g_autoptr(GTimer) timer = g_timer_new ();

	g_return_val_if_fail (FU_IS_DEVICE_LIST (self), FALSE);
	g_return_val_if_fail (FU_IS_DEVICE (device), FALSE);
	g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

	/* not found */
	item = fu_device_list_find_by_device (self, device);
	if (item == NULL)
		return TRUE;

	/* not required, or possibly literally just happened */
	if (!fu_device_has_flag (item->device, FWUPD_DEVICE_FLAG_WAIT_FOR_REPLUG)) {
		g_debug ("no replug or re-enumerate required");
		return TRUE;
	}

	/* check that no other devices are waiting for replug too */
	for (guint i = 0; i < self->devices->len; i++) {
		FuDeviceItem *item_tmp = g_ptr_array_index (self->devices, i);
		if (item_tmp->device != device &&
		    fu_device_has_flag (item_tmp->device, FWUPD_DEVICE_FLAG_WAIT_FOR_REPLUG)) {
			g_warning ("%s is wait-for-replug when %s scheduled, unsetting",
				   fu_device_get_id (item_tmp->device),
				   fu_device_get_id (device));
			fu_device_remove_flag (item_tmp->device, FWUPD_DEVICE_FLAG_WAIT_FOR_REPLUG);
		}
	}

	/* plugin did not specify */
	remove_delay = fu_device_get_remove_delay (device);
	if (remove_delay == 0) {
		remove_delay = FU_DEVICE_REMOVE_DELAY_RE_ENUMERATE;
		g_warning ("plugin %s did not specify a remove delay for %s, "
			   "so guessing we should wait %ums for replug",
			   fu_device_get_plugin (device),
			   fu_device_get_id (device),
			   remove_delay);
	} else {
		g_debug ("waiting %ums for replug", remove_delay);
	}

	/* time to unplug and then re-plug */
	do {
		/* count how many devices are in the remove waiting state */
		wait_removed = fu_device_list_devices_wait_removed (self);
		if (wait_removed != wait_removed_old) {
			g_debug ("devices in wait_removed: %u -> %u",
				 wait_removed_old, wait_removed);
			wait_removed_old = wait_removed;
		}
		g_usleep (1000);
		g_main_context_iteration (NULL, FALSE);
		if (!fu_device_has_flag (item->device, FWUPD_DEVICE_FLAG_WAIT_FOR_REPLUG) &&
		    wait_removed == 0)
			break;
	} while (g_timer_elapsed (timer, NULL) * 1000.f < remove_delay);

	/* device was not added back to the device list */
	if (fu_device_has_flag (item->device, FWUPD_DEVICE_FLAG_WAIT_FOR_REPLUG)) {
		g_set_error (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_NOT_FOUND,
			     "device %s did not come back",
			     fu_device_get_id (device));
		fu_device_remove_flag (item->device, FWUPD_DEVICE_FLAG_WAIT_FOR_REPLUG);
		return FALSE;
	}

	/* check that no other devices are waiting for replug instead */
	for (guint i = 0; i < self->devices->len; i++) {
		FuDeviceItem *item_tmp = g_ptr_array_index (self->devices, i);
		if (fu_device_has_flag (item_tmp->device, FWUPD_DEVICE_FLAG_WAIT_FOR_REPLUG)) {
			g_warning ("%s is wait-for-replug when %s performed",
				   fu_device_get_id (item_tmp->device),
				   fu_device_get_id (device));
		}
	}

	/* the loop was quit without the timer */
	g_debug ("waited for replug");
	return TRUE;
}

/**
 * fu_device_list_get_by_id:
 * @self: A #FuDeviceList
 * @device_id: A device ID, typically a SHA1 hash
 * @error: A #GError, or %NULL
 *
 * Finds a specific device using the ID string. This function also supports
 * using abbreviated hashes.
 *
 * Returns: (transfer full): a device, or %NULL if not found
 *
 * Since: 1.0.2
 **/
FuDevice *
fu_device_list_get_by_id (FuDeviceList *self, const gchar *device_id, GError **error)
{
	FuDeviceItem *item;
	gboolean multiple_matches = FALSE;

	g_return_val_if_fail (FU_IS_DEVICE_LIST (self), NULL);
	g_return_val_if_fail (device_id != NULL, NULL);
	g_return_val_if_fail (error == NULL || *error == NULL, NULL);

	/* multiple things matched */
	item = fu_device_list_find_by_id (self, device_id, &multiple_matches);
	if (multiple_matches) {
		g_set_error (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_NOT_SUPPORTED,
			     "device ID %s was not unique",
			     device_id);
		return NULL;
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

	/* something found */
	return g_object_ref (item->device);
}

static void
fu_device_list_item_free (FuDeviceItem *item)
{
	if (item->remove_id != 0)
		g_source_remove (item->remove_id);
	if (item->device_old != NULL)
		g_object_unref (item->device_old);
	fu_device_list_item_set_device (item, NULL);
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
	g_rw_lock_init (&self->devices_mutex);
}

static void
fu_device_list_finalize (GObject *obj)
{
	FuDeviceList *self = FU_DEVICE_LIST (obj);

	g_rw_lock_clear (&self->devices_mutex);
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
