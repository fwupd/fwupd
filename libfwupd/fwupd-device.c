/*
 * Copyright 2015 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include <gio/gio.h>
#include <string.h>

#include "fwupd-codec.h"
#include "fwupd-common-private.h"
#include "fwupd-device-private.h"
#include "fwupd-enums-private.h"
#include "fwupd-error.h"

/**
 * FwupdDevice:
 *
 * A physical device on the host with optionally updatable firmware.
 *
 * See also: [class@FwupdRelease]
 */

static void
fwupd_device_finalize(GObject *object);

typedef struct {
	gchar *id;
	gchar *parent_id;
	gchar *composite_id;
	guint64 created;
	guint64 modified;
	guint64 flags;
	guint64 request_flags;
	guint64 problems;
	GPtrArray *guids;	 /* (nullable) (element-type utf-8) */
	GPtrArray *vendor_ids;	 /* (nullable) (element-type utf-8) */
	GPtrArray *protocols;	 /* (nullable) (element-type utf-8) */
	GPtrArray *instance_ids; /* (nullable) (element-type utf-8) */
	GPtrArray *icons;	 /* (nullable) (element-type utf-8) */
	GPtrArray *issues;	 /* (nullable) (element-type utf-8) */
	gchar *name;
	gchar *serial;
	gchar *summary;
	gchar *branch;
	gchar *vendor;
	gchar *homepage;
	gchar *plugin;
	gchar *version;
	gchar *version_lowest;
	gchar *version_bootloader;
	FwupdVersionFormat version_format;
	guint64 version_raw;
	guint64 version_build_date;
	guint64 version_lowest_raw;
	guint64 version_bootloader_raw;
	GPtrArray *checksums; /* (nullable) (element-type utf-8) */
	GPtrArray *children;  /* (nullable) (element-type FuDevice) */
	guint32 flashes_left;
	guint32 battery_level;
	guint32 battery_threshold;
	guint32 install_duration;
	FwupdUpdateState update_state;
	gchar *update_error;
	FwupdStatus status;
	guint percentage;
	GPtrArray *releases; /* (nullable) (element-type FwupdRelease) */
	FwupdDevice *parent; /* noref */
} FwupdDevicePrivate;

enum {
	PROP_0,
	PROP_ID,
	PROP_VERSION,
	PROP_VERSION_FORMAT,
	PROP_FLAGS,
	PROP_REQUEST_FLAGS,
	PROP_STATUS,
	PROP_PERCENTAGE,
	PROP_PARENT,
	PROP_UPDATE_STATE,
	PROP_UPDATE_ERROR,
	PROP_BATTERY_LEVEL,
	PROP_BATTERY_THRESHOLD,
	PROP_PROBLEMS,
	PROP_LAST
};

static void
fwupd_device_codec_iface_init(FwupdCodecInterface *iface);

G_DEFINE_TYPE_EXTENDED(FwupdDevice,
		       fwupd_device,
		       G_TYPE_OBJECT,
		       0,
		       G_ADD_PRIVATE(FwupdDevice)
			   G_IMPLEMENT_INTERFACE(FWUPD_TYPE_CODEC, fwupd_device_codec_iface_init));

#define GET_PRIVATE(o) (fwupd_device_get_instance_private(o))

#define FWUPD_BATTERY_THRESHOLD_DEFAULT 10 /* % */

static void
fwupd_device_ensure_checksums(FwupdDevice *self)
{
	FwupdDevicePrivate *priv = GET_PRIVATE(self);
	if (priv->checksums == NULL)
		priv->checksums = g_ptr_array_new_with_free_func(g_free);
}

/**
 * fwupd_device_get_checksums:
 * @self: a #FwupdDevice
 *
 * Gets the device checksums.
 *
 * Returns: (element-type utf8) (transfer none): the checksums, which may be empty
 *
 * Since: 0.9.3
 **/
GPtrArray *
fwupd_device_get_checksums(FwupdDevice *self)
{
	FwupdDevicePrivate *priv = GET_PRIVATE(self);
	g_return_val_if_fail(FWUPD_IS_DEVICE(self), NULL);
	fwupd_device_ensure_checksums(self);
	return priv->checksums;
}

/**
 * fwupd_device_has_checksum:
 * @self: a #FwupdDevice
 * @checksum: (not nullable): the device checksum
 *
 * Finds out if the device has this specific checksum.
 *
 * Returns: %TRUE if the checksum name is found
 *
 * Since: 1.8.7
 **/
gboolean
fwupd_device_has_checksum(FwupdDevice *self, const gchar *checksum)
{
	FwupdDevicePrivate *priv = GET_PRIVATE(self);

	g_return_val_if_fail(FWUPD_IS_DEVICE(self), FALSE);
	g_return_val_if_fail(checksum != NULL, FALSE);

	if (priv->checksums == NULL)
		return FALSE;
	for (guint i = 0; i < priv->checksums->len; i++) {
		const gchar *checksum_tmp = g_ptr_array_index(priv->checksums, i);
		if (g_strcmp0(checksum, checksum_tmp) == 0)
			return TRUE;
	}
	return FALSE;
}

/**
 * fwupd_device_add_checksum:
 * @self: a #FwupdDevice
 * @checksum: (not nullable): the device checksum
 *
 * Adds a device checksum.
 *
 * Since: 0.9.3
 **/
void
fwupd_device_add_checksum(FwupdDevice *self, const gchar *checksum)
{
	FwupdDevicePrivate *priv = GET_PRIVATE(self);
	g_return_if_fail(FWUPD_IS_DEVICE(self));
	g_return_if_fail(checksum != NULL);

	if (fwupd_device_has_checksum(self, checksum))
		return;
	fwupd_device_ensure_checksums(self);
	g_ptr_array_add(priv->checksums, g_strdup(checksum));
}

static void
fwupd_device_ensure_issues(FwupdDevice *self)
{
	FwupdDevicePrivate *priv = GET_PRIVATE(self);
	if (priv->issues == NULL)
		priv->issues = g_ptr_array_new_with_free_func(g_free);
}

/**
 * fwupd_device_get_issues:
 * @self: a #FwupdDevice
 *
 * Gets the list of issues currently affecting this device.
 *
 * Returns: (element-type utf8) (transfer none): the issues, which may be empty
 *
 * Since: 1.7.6
 **/
GPtrArray *
fwupd_device_get_issues(FwupdDevice *self)
{
	FwupdDevicePrivate *priv = GET_PRIVATE(self);
	g_return_val_if_fail(FWUPD_IS_DEVICE(self), NULL);
	fwupd_device_ensure_issues(self);
	return priv->issues;
}

/**
 * fwupd_device_add_issue:
 * @self: a #FwupdDevice
 * @issue: (not nullable): the update issue, e.g. `CVE-2019-12345`
 *
 * Adds an current issue to this device.
 *
 * Since: 1.7.6
 **/
void
fwupd_device_add_issue(FwupdDevice *self, const gchar *issue)
{
	FwupdDevicePrivate *priv = GET_PRIVATE(self);
	g_return_if_fail(FWUPD_IS_DEVICE(self));
	g_return_if_fail(issue != NULL);

	fwupd_device_ensure_issues(self);
	for (guint i = 0; i < priv->issues->len; i++) {
		const gchar *issue_tmp = g_ptr_array_index(priv->issues, i);
		if (g_strcmp0(issue_tmp, issue) == 0)
			return;
	}
	g_ptr_array_add(priv->issues, g_strdup(issue));
}

static void
fwupd_device_ensure_children(FwupdDevice *self)
{
	FwupdDevicePrivate *priv = GET_PRIVATE(self);
	if (priv->children == NULL)
		priv->children = g_ptr_array_new_with_free_func((GDestroyNotify)g_object_unref);
}

/**
 * fwupd_device_get_children:
 * @self: a #FwupdDevice
 *
 * Gets the device children. These can only be assigned using fwupd_device_set_parent().
 *
 * Returns: (element-type FwupdDevice) (transfer none): the children, which may be empty
 *
 * Since: 1.3.7
 **/
GPtrArray *
fwupd_device_get_children(FwupdDevice *self)
{
	FwupdDevicePrivate *priv = GET_PRIVATE(self);
	g_return_val_if_fail(FWUPD_IS_DEVICE(self), NULL);
	fwupd_device_ensure_children(self);
	return priv->children;
}

/**
 * fwupd_device_get_summary:
 * @self: a #FwupdDevice
 *
 * Gets the device summary.
 *
 * Returns: the device summary, or %NULL if unset
 *
 * Since: 0.9.3
 **/
const gchar *
fwupd_device_get_summary(FwupdDevice *self)
{
	FwupdDevicePrivate *priv = GET_PRIVATE(self);
	g_return_val_if_fail(FWUPD_IS_DEVICE(self), NULL);
	return priv->summary;
}

/**
 * fwupd_device_set_summary:
 * @self: a #FwupdDevice
 * @summary: (nullable): the device one line summary
 *
 * Sets the device summary.
 *
 * Since: 0.9.3
 **/
void
fwupd_device_set_summary(FwupdDevice *self, const gchar *summary)
{
	FwupdDevicePrivate *priv = GET_PRIVATE(self);
	g_return_if_fail(FWUPD_IS_DEVICE(self));

	/* not changed */
	if (g_strcmp0(priv->summary, summary) == 0)
		return;

	g_free(priv->summary);
	priv->summary = g_strdup(summary);
}

/**
 * fwupd_device_get_branch:
 * @self: a #FwupdDevice
 *
 * Gets the current device branch.
 *
 * Returns: the device branch, or %NULL if unset
 *
 * Since: 1.5.0
 **/
const gchar *
fwupd_device_get_branch(FwupdDevice *self)
{
	FwupdDevicePrivate *priv = GET_PRIVATE(self);
	g_return_val_if_fail(FWUPD_IS_DEVICE(self), NULL);
	return priv->branch;
}

/**
 * fwupd_device_set_branch:
 * @self: a #FwupdDevice
 * @branch: (nullable): the device one line branch
 *
 * Sets the current device branch.
 *
 * Since: 1.5.0
 **/
void
fwupd_device_set_branch(FwupdDevice *self, const gchar *branch)
{
	FwupdDevicePrivate *priv = GET_PRIVATE(self);
	g_return_if_fail(FWUPD_IS_DEVICE(self));

	/* not changed */
	if (g_strcmp0(priv->branch, branch) == 0)
		return;

	g_free(priv->branch);
	priv->branch = g_strdup(branch);
}

/**
 * fwupd_device_get_serial:
 * @self: a #FwupdDevice
 *
 * Gets the serial number for the device.
 *
 * Returns: a string value, or %NULL if never set.
 *
 * Since: 1.1.2
 **/
const gchar *
fwupd_device_get_serial(FwupdDevice *self)
{
	FwupdDevicePrivate *priv = GET_PRIVATE(self);
	g_return_val_if_fail(FWUPD_IS_DEVICE(self), NULL);
	return priv->serial;
}

/**
 * fwupd_device_set_serial:
 * @self: a #FwupdDevice
 * @serial: (nullable): the device serial number
 *
 * Sets the serial number for the device.
 *
 * Since: 1.1.2
 **/
void
fwupd_device_set_serial(FwupdDevice *self, const gchar *serial)
{
	FwupdDevicePrivate *priv = GET_PRIVATE(self);
	g_return_if_fail(FWUPD_IS_DEVICE(self));

	/* not changed */
	if (g_strcmp0(priv->serial, serial) == 0)
		return;

	g_free(priv->serial);
	priv->serial = g_strdup(serial);
}

/**
 * fwupd_device_get_id:
 * @self: a #FwupdDevice
 *
 * Gets the ID.
 *
 * Returns: the ID, or %NULL if unset
 *
 * Since: 0.9.3
 **/
const gchar *
fwupd_device_get_id(FwupdDevice *self)
{
	FwupdDevicePrivate *priv = GET_PRIVATE(self);
	g_return_val_if_fail(FWUPD_IS_DEVICE(self), NULL);
	return priv->id;
}

/**
 * fwupd_device_set_id:
 * @self: a #FwupdDevice
 * @id: (nullable): the device ID, usually a SHA1 hash
 *
 * Sets the ID.
 *
 * Since: 0.9.3
 **/
void
fwupd_device_set_id(FwupdDevice *self, const gchar *id)
{
	FwupdDevicePrivate *priv = GET_PRIVATE(self);
	g_return_if_fail(FWUPD_IS_DEVICE(self));

	/* not changed */
	if (g_strcmp0(priv->id, id) == 0)
		return;

	/* sanity check */
	if (!fwupd_device_id_is_valid(id)) {
		g_critical("%s is not a valid device ID", id);
		return;
	}

	g_free(priv->id);
	priv->id = g_strdup(id);
	g_object_notify(G_OBJECT(self), "id");
}

/**
 * fwupd_device_get_parent_id:
 * @self: a #FwupdDevice
 *
 * Gets the parent ID.
 *
 * Returns: the parent ID, or %NULL if unset
 *
 * Since: 1.0.8
 **/
const gchar *
fwupd_device_get_parent_id(FwupdDevice *self)
{
	FwupdDevicePrivate *priv = GET_PRIVATE(self);
	g_return_val_if_fail(FWUPD_IS_DEVICE(self), NULL);
	return priv->parent_id;
}

/**
 * fwupd_device_set_parent_id:
 * @self: a #FwupdDevice
 * @parent_id: (nullable): the device ID, usually a SHA1 hash
 *
 * Sets the parent ID.
 *
 * Since: 1.0.8
 **/
void
fwupd_device_set_parent_id(FwupdDevice *self, const gchar *parent_id)
{
	FwupdDevicePrivate *priv = GET_PRIVATE(self);
	g_return_if_fail(FWUPD_IS_DEVICE(self));

	/* not changed */
	if (g_strcmp0(priv->parent_id, parent_id) == 0)
		return;

	/* sanity check */
	if (!fwupd_device_id_is_valid(parent_id)) {
		g_critical("%s is not a valid device ID", parent_id);
		return;
	}

	g_free(priv->parent_id);
	priv->parent_id = g_strdup(parent_id);
}

/**
 * fwupd_device_get_composite_id:
 * @self: a #FwupdDevice
 *
 * Gets the composite ID, falling back to the device ID if unset.
 *
 * The composite ID will be the same value for all parent, child and sibling
 * devices.
 *
 * Returns: (nullable): the composite ID
 *
 * Since: 1.6.0
 **/
const gchar *
fwupd_device_get_composite_id(FwupdDevice *self)
{
	FwupdDevicePrivate *priv = GET_PRIVATE(self);
	g_return_val_if_fail(FWUPD_IS_DEVICE(self), NULL);
	if (priv->composite_id != NULL)
		return priv->composite_id;
	return priv->id;
}

/**
 * fwupd_device_set_composite_id:
 * @self: a #FwupdDevice
 * @composite_id: (nullable): a device ID
 *
 * Sets the composite ID, which is usually a SHA1 hash of a grandparent or
 * parent device.
 *
 * Since: 1.6.0
 **/
void
fwupd_device_set_composite_id(FwupdDevice *self, const gchar *composite_id)
{
	FwupdDevicePrivate *priv = GET_PRIVATE(self);
	g_return_if_fail(FWUPD_IS_DEVICE(self));

	/* not changed */
	if (g_strcmp0(priv->composite_id, composite_id) == 0)
		return;

	/* sanity check */
	if (!fwupd_device_id_is_valid(composite_id)) {
		g_critical("%s is not a valid device ID", composite_id);
		return;
	}

	g_free(priv->composite_id);
	priv->composite_id = g_strdup(composite_id);
}

/**
 * fwupd_device_get_parent:
 * @self: a #FwupdDevice
 *
 * Gets the parent.
 *
 * Returns: (transfer none): the parent device, or %NULL if unset
 *
 * Since: 1.0.8
 **/
FwupdDevice *
fwupd_device_get_parent(FwupdDevice *self)
{
	FwupdDevicePrivate *priv = GET_PRIVATE(self);
	g_return_val_if_fail(FWUPD_IS_DEVICE(self), NULL);
	return priv->parent;
}

/**
 * fwupd_device_get_root:
 * @self: a #FwupdDevice
 *
 * Gets the device root.
 *
 * Returns: (transfer none): the root device, or %NULL if unset
 *
 * Since: 1.7.4
 **/
FwupdDevice *
fwupd_device_get_root(FwupdDevice *self)
{
	g_return_val_if_fail(FWUPD_IS_DEVICE(self), NULL);
	while (1) {
		FwupdDevicePrivate *priv = GET_PRIVATE(self);
		if (priv->parent == NULL)
			break;
		self = priv->parent;
	}
	return self;
}

/**
 * fwupd_device_set_parent:
 * @self: a #FwupdDevice
 * @parent: (nullable): another #FwupdDevice
 *
 * Sets the parent. Only used internally.
 *
 * Since: 1.0.8
 **/
void
fwupd_device_set_parent(FwupdDevice *self, FwupdDevice *parent)
{
	FwupdDevicePrivate *priv = GET_PRIVATE(self);
	g_return_if_fail(FWUPD_IS_DEVICE(self));
	g_return_if_fail(self != parent);

	if (priv->parent != NULL)
		g_object_remove_weak_pointer(G_OBJECT(priv->parent), (gpointer *)&priv->parent);
	if (parent != NULL)
		g_object_add_weak_pointer(G_OBJECT(parent), (gpointer *)&priv->parent);
	priv->parent = parent;

	/* this is what goes over D-Bus */
	fwupd_device_set_parent_id(self, parent != NULL ? fwupd_device_get_id(parent) : NULL);
}

static void
fwupd_device_child_finalized_cb(gpointer data, GObject *where_the_object_was)
{
	FwupdDevice *self = FWUPD_DEVICE(data);
	g_critical("FuDevice child %p was finalized while still having parent %s [%s]!",
		   where_the_object_was,
		   fwupd_device_get_name(self),
		   fwupd_device_get_id(self));
}

/**
 * fwupd_device_add_child:
 * @self: a #FwupdDevice
 * @child: (not nullable): Another #FwupdDevice
 *
 * Adds a child device. An child device is logically linked to the primary
 * device in some way.
 *
 * NOTE: You should never call this function from user code, it is for daemon
 * use only. Only use fwupd_device_set_parent() to set up a logical tree.
 *
 * Since: 1.5.1
 **/
void
fwupd_device_add_child(FwupdDevice *self, FwupdDevice *child)
{
	FwupdDevicePrivate *priv = GET_PRIVATE(self);
	g_return_if_fail(FWUPD_IS_DEVICE(self));
	g_return_if_fail(FWUPD_IS_DEVICE(child));
	g_return_if_fail(self != child);

	/* add if the child does not already exist */
	fwupd_device_ensure_children(self);
	for (guint i = 0; i < priv->children->len; i++) {
		FwupdDevice *devtmp = g_ptr_array_index(priv->children, i);
		if (devtmp == child)
			return;
	}
	g_object_weak_ref(G_OBJECT(child), fwupd_device_child_finalized_cb, self);
	g_ptr_array_add(priv->children, g_object_ref(child));
}

/**
 * fwupd_device_remove_child:
 * @self: a #FwupdDevice
 * @child: Another #FwupdDevice
 *
 * Removes a child device.
 *
 * NOTE: You should never call this function from user code, it is for daemon
 * use only.
 *
 * Since: 1.6.2
 **/
void
fwupd_device_remove_child(FwupdDevice *self, FwupdDevice *child)
{
	FwupdDevicePrivate *priv = GET_PRIVATE(self);

	/* remove if the child exists */
	if (priv->children == NULL)
		return;
	for (guint i = 0; i < priv->children->len; i++) {
		FwupdDevice *child_tmp = g_ptr_array_index(priv->children, i);
		if (child_tmp == child) {
			g_object_weak_unref(G_OBJECT(child), fwupd_device_child_finalized_cb, self);
			g_ptr_array_remove_index(priv->children, i);
			return;
		}
	}
}

/**
 * fwupd_device_remove_children:
 * @self: a #FwupdDevice
 *
 * Removes all child devices.
 *
 * NOTE: You should never call this function from user code, it is for daemon
 * use only.
 *
 * Since: 2.0.0
 **/
void
fwupd_device_remove_children(FwupdDevice *self)
{
	FwupdDevicePrivate *priv = GET_PRIVATE(self);

	g_return_if_fail(FWUPD_IS_DEVICE(self));

	if (priv->children == NULL)
		return;
	for (guint i = 0; i < priv->children->len; i++) {
		FwupdDevice *child = g_ptr_array_index(priv->children, i);
		g_object_weak_unref(G_OBJECT(child), fwupd_device_child_finalized_cb, self);
	}
	g_ptr_array_set_size(priv->children, 0);
}

static void
fwupd_device_ensure_guids(FwupdDevice *self)
{
	FwupdDevicePrivate *priv = GET_PRIVATE(self);
	if (priv->guids == NULL)
		priv->guids = g_ptr_array_new_with_free_func(g_free);
}

/**
 * fwupd_device_get_guids:
 * @self: a #FwupdDevice
 *
 * Gets the GUIDs.
 *
 * Returns: (element-type utf8) (transfer none): the GUIDs
 *
 * Since: 0.9.3
 **/
GPtrArray *
fwupd_device_get_guids(FwupdDevice *self)
{
	FwupdDevicePrivate *priv = GET_PRIVATE(self);
	g_return_val_if_fail(FWUPD_IS_DEVICE(self), NULL);
	fwupd_device_ensure_guids(self);
	return priv->guids;
}

/**
 * fwupd_device_has_guid:
 * @self: a #FwupdDevice
 * @guid: (not nullable): the GUID, e.g. `2082b5e0-7a64-478a-b1b2-e3404fab6dad`
 *
 * Finds out if the device has this specific GUID.
 *
 * Returns: %TRUE if the GUID is found
 *
 * Since: 0.9.3
 **/
gboolean
fwupd_device_has_guid(FwupdDevice *self, const gchar *guid)
{
	FwupdDevicePrivate *priv = GET_PRIVATE(self);

	g_return_val_if_fail(FWUPD_IS_DEVICE(self), FALSE);
	g_return_val_if_fail(guid != NULL, FALSE);

	if (priv->guids == NULL)
		return FALSE;
	for (guint i = 0; i < priv->guids->len; i++) {
		const gchar *guid_tmp = g_ptr_array_index(priv->guids, i);
		if (g_strcmp0(guid, guid_tmp) == 0)
			return TRUE;
	}
	return FALSE;
}

/**
 * fwupd_device_add_guid:
 * @self: a #FwupdDevice
 * @guid: the GUID, e.g. `2082b5e0-7a64-478a-b1b2-e3404fab6dad`
 *
 * Adds the GUID if it does not already exist.
 *
 * Since: 0.9.3
 **/
void
fwupd_device_add_guid(FwupdDevice *self, const gchar *guid)
{
	FwupdDevicePrivate *priv = GET_PRIVATE(self);
	g_return_if_fail(FWUPD_IS_DEVICE(self));
	g_return_if_fail(guid != NULL);
	if (fwupd_device_has_guid(self, guid))
		return;
	fwupd_device_ensure_guids(self);
	g_ptr_array_add(priv->guids, g_strdup(guid));
}

/**
 * fwupd_device_get_guid_default:
 * @self: a #FwupdDevice
 *
 * Gets the default GUID.
 *
 * Returns: the GUID, or %NULL if unset
 *
 * Since: 0.9.3
 **/
const gchar *
fwupd_device_get_guid_default(FwupdDevice *self)
{
	FwupdDevicePrivate *priv = GET_PRIVATE(self);
	g_return_val_if_fail(FWUPD_IS_DEVICE(self), NULL);
	if (priv->guids == NULL || priv->guids->len == 0)
		return NULL;
	return g_ptr_array_index(priv->guids, 0);
}

static void
fwupd_device_ensure_instance_ids(FwupdDevice *self)
{
	FwupdDevicePrivate *priv = GET_PRIVATE(self);
	if (priv->instance_ids == NULL)
		priv->instance_ids = g_ptr_array_new_with_free_func(g_free);
}

/**
 * fwupd_device_get_instance_ids:
 * @self: a #FwupdDevice
 *
 * Gets the instance IDs.
 *
 * Returns: (element-type utf8) (transfer none): the instance IDs
 *
 * Since: 1.2.5
 **/
GPtrArray *
fwupd_device_get_instance_ids(FwupdDevice *self)
{
	FwupdDevicePrivate *priv = GET_PRIVATE(self);
	g_return_val_if_fail(FWUPD_IS_DEVICE(self), NULL);
	fwupd_device_ensure_instance_ids(self);
	return priv->instance_ids;
}

/**
 * fwupd_device_has_instance_id:
 * @self: a #FwupdDevice
 * @instance_id: (not nullable): the instance ID, e.g. `PCI\VEN_10EC&DEV_525A`
 *
 * Finds out if the device has this specific instance ID.
 *
 * Returns: %TRUE if the instance ID is found
 *
 * Since: 1.2.5
 **/
gboolean
fwupd_device_has_instance_id(FwupdDevice *self, const gchar *instance_id)
{
	FwupdDevicePrivate *priv = GET_PRIVATE(self);

	g_return_val_if_fail(FWUPD_IS_DEVICE(self), FALSE);
	g_return_val_if_fail(instance_id != NULL, FALSE);

	if (priv->instance_ids == NULL)
		return FALSE;
	for (guint i = 0; i < priv->instance_ids->len; i++) {
		const gchar *instance_id_tmp = g_ptr_array_index(priv->instance_ids, i);
		if (g_strcmp0(instance_id, instance_id_tmp) == 0)
			return TRUE;
	}
	return FALSE;
}

/**
 * fwupd_device_add_instance_id:
 * @self: a #FwupdDevice
 * @instance_id: (not nullable): the instance ID, e.g. `PCI\VEN_10EC&DEV_525A`
 *
 * Adds the instance ID if it does not already exist.
 *
 * Since: 1.2.5
 **/
void
fwupd_device_add_instance_id(FwupdDevice *self, const gchar *instance_id)
{
	FwupdDevicePrivate *priv = GET_PRIVATE(self);
	g_return_if_fail(FWUPD_IS_DEVICE(self));
	g_return_if_fail(instance_id != NULL);
	if (fwupd_device_has_instance_id(self, instance_id))
		return;
	fwupd_device_ensure_instance_ids(self);
	g_ptr_array_add(priv->instance_ids, g_strdup(instance_id));
}

static void
fwupd_device_ensure_icons(FwupdDevice *self)
{
	FwupdDevicePrivate *priv = GET_PRIVATE(self);
	if (priv->icons == NULL)
		priv->icons = g_ptr_array_new_with_free_func(g_free);
}

/**
 * fwupd_device_get_icons:
 * @self: a #FwupdDevice
 *
 * Gets the icon names to use for the device.
 *
 * NOTE: Icons specified without a full path are stock icons and should
 * be loaded from the users icon theme.
 *
 * Returns: (element-type utf8) (transfer none): an array of icon names
 *
 * Since: 0.9.8
 **/
GPtrArray *
fwupd_device_get_icons(FwupdDevice *self)
{
	FwupdDevicePrivate *priv = GET_PRIVATE(self);
	g_return_val_if_fail(FWUPD_IS_DEVICE(self), NULL);
	fwupd_device_ensure_icons(self);
	return priv->icons;
}

/**
 * fwupd_device_has_icon:
 * @self: a #FwupdDevice
 * @icon: the icon name, e.g. `input-mouse` or `/usr/share/icons/foo.png`
 *
 * Finds out if the device has this specific icon.
 *
 * Returns: %TRUE if the icon name is found
 *
 * Since: 1.6.2
 **/
gboolean
fwupd_device_has_icon(FwupdDevice *self, const gchar *icon)
{
	FwupdDevicePrivate *priv = GET_PRIVATE(self);
	if (priv->icons == NULL)
		return FALSE;
	for (guint i = 0; i < priv->icons->len; i++) {
		const gchar *icon_tmp = g_ptr_array_index(priv->icons, i);
		if (g_strcmp0(icon, icon_tmp) == 0)
			return TRUE;
	}
	return FALSE;
}

/**
 * fwupd_device_add_icon:
 * @self: a #FwupdDevice
 * @icon: (not nullable): the icon name, e.g. `input-mouse` or `/usr/share/icons/foo.png`
 *
 * Adds the icon name if it does not already exist.
 *
 * Since: 0.9.8
 **/
void
fwupd_device_add_icon(FwupdDevice *self, const gchar *icon)
{
	FwupdDevicePrivate *priv = GET_PRIVATE(self);
	g_return_if_fail(FWUPD_IS_DEVICE(self));
	g_return_if_fail(icon != NULL);
	if (fwupd_device_has_icon(self, icon))
		return;
	fwupd_device_ensure_icons(self);
	g_ptr_array_add(priv->icons, g_strdup(icon));
}

/**
 * fwupd_device_get_name:
 * @self: a #FwupdDevice
 *
 * Gets the device name.
 *
 * Returns: the device name, or %NULL if unset
 *
 * Since: 0.9.3
 **/
const gchar *
fwupd_device_get_name(FwupdDevice *self)
{
	FwupdDevicePrivate *priv = GET_PRIVATE(self);
	g_return_val_if_fail(FWUPD_IS_DEVICE(self), NULL);
	return priv->name;
}

/**
 * fwupd_device_set_name:
 * @self: a #FwupdDevice
 * @name: (nullable): the device name, e.g. `ColorHug2`
 *
 * Sets the device name.
 *
 * Since: 0.9.3
 **/
void
fwupd_device_set_name(FwupdDevice *self, const gchar *name)
{
	FwupdDevicePrivate *priv = GET_PRIVATE(self);
	g_return_if_fail(FWUPD_IS_DEVICE(self));

	/* not changed */
	if (g_strcmp0(priv->name, name) == 0)
		return;

	g_free(priv->name);
	priv->name = g_strdup(name);
}

/**
 * fwupd_device_get_vendor:
 * @self: a #FwupdDevice
 *
 * Gets the device vendor.
 *
 * Returns: the device vendor, or %NULL if unset
 *
 * Since: 0.9.3
 **/
const gchar *
fwupd_device_get_vendor(FwupdDevice *self)
{
	FwupdDevicePrivate *priv = GET_PRIVATE(self);
	g_return_val_if_fail(FWUPD_IS_DEVICE(self), NULL);
	return priv->vendor;
}

/**
 * fwupd_device_set_vendor:
 * @self: a #FwupdDevice
 * @vendor: (nullable): the vendor
 *
 * Sets the device vendor.
 *
 * Since: 0.9.3
 **/
void
fwupd_device_set_vendor(FwupdDevice *self, const gchar *vendor)
{
	FwupdDevicePrivate *priv = GET_PRIVATE(self);
	g_return_if_fail(FWUPD_IS_DEVICE(self));

	/* not changed */
	if (g_strcmp0(priv->vendor, vendor) == 0)
		return;

	g_free(priv->vendor);
	priv->vendor = g_strdup(vendor);
}

static void
fwupd_device_ensure_vendor_ids(FwupdDevice *self)
{
	FwupdDevicePrivate *priv = GET_PRIVATE(self);
	if (priv->vendor_ids == NULL)
		priv->vendor_ids = g_ptr_array_new_with_free_func(g_free);
}

/**
 * fwupd_device_get_vendor_ids:
 * @self: a #FwupdDevice
 *
 * Gets the device vendor ID.
 *
 * Returns: (element-type utf8) (transfer none): the device vendor ID
 *
 * Since: 1.5.5
 **/
GPtrArray *
fwupd_device_get_vendor_ids(FwupdDevice *self)
{
	FwupdDevicePrivate *priv = GET_PRIVATE(self);
	g_return_val_if_fail(FWUPD_IS_DEVICE(self), NULL);
	fwupd_device_ensure_vendor_ids(self);
	return priv->vendor_ids;
}

/**
 * fwupd_device_has_vendor_id:
 * @self: a #FwupdDevice
 * @vendor_id: (not nullable): the vendor ID, e.g. 'USB:0x1234'
 *
 * Finds out if the device has this specific vendor ID.
 *
 * Returns: %TRUE if the vendor ID is found
 *
 * Since: 1.5.5
 **/
gboolean
fwupd_device_has_vendor_id(FwupdDevice *self, const gchar *vendor_id)
{
	FwupdDevicePrivate *priv = GET_PRIVATE(self);

	g_return_val_if_fail(FWUPD_IS_DEVICE(self), FALSE);
	g_return_val_if_fail(vendor_id != NULL, FALSE);

	if (priv->vendor_ids == NULL)
		return FALSE;
	for (guint i = 0; i < priv->vendor_ids->len; i++) {
		const gchar *vendor_id_tmp = g_ptr_array_index(priv->vendor_ids, i);
		if (g_strcmp0(vendor_id, vendor_id_tmp) == 0)
			return TRUE;
	}
	return FALSE;
}

/**
 * fwupd_device_add_vendor_id:
 * @self: a #FwupdDevice
 * @vendor_id: (not nullable): the ID, e.g. 'USB:0x1234'
 *
 * Adds a device vendor ID.
 *
 * Since: 1.5.5
 **/
void
fwupd_device_add_vendor_id(FwupdDevice *self, const gchar *vendor_id)
{
	FwupdDevicePrivate *priv = GET_PRIVATE(self);

	g_return_if_fail(FWUPD_IS_DEVICE(self));
	g_return_if_fail(vendor_id != NULL);

	if (fwupd_device_has_vendor_id(self, vendor_id))
		return;
	fwupd_device_ensure_vendor_ids(self);
	g_ptr_array_add(priv->vendor_ids, g_strdup(vendor_id));
}

/**
 * fwupd_device_get_version:
 * @self: a #FwupdDevice
 *
 * Gets the device version.
 *
 * Returns: the device version, or %NULL if unset
 *
 * Since: 0.9.3
 **/
const gchar *
fwupd_device_get_version(FwupdDevice *self)
{
	FwupdDevicePrivate *priv = GET_PRIVATE(self);
	g_return_val_if_fail(FWUPD_IS_DEVICE(self), NULL);
	return priv->version;
}

/**
 * fwupd_device_set_version:
 * @self: a #FwupdDevice
 * @version: (nullable): the device version, e.g. `1.2.3`
 *
 * Sets the device version.
 *
 * Since: 0.9.3
 **/
void
fwupd_device_set_version(FwupdDevice *self, const gchar *version)
{
	FwupdDevicePrivate *priv = GET_PRIVATE(self);
	g_return_if_fail(FWUPD_IS_DEVICE(self));

	/* not changed */
	if (g_strcmp0(priv->version, version) == 0)
		return;

	g_free(priv->version);
	priv->version = g_strdup(version);
	g_object_notify(G_OBJECT(self), "version");
}

/**
 * fwupd_device_get_version_lowest:
 * @self: a #FwupdDevice
 *
 * Gets the lowest version of firmware the device will accept.
 *
 * Returns: the device version_lowest, or %NULL if unset
 *
 * Since: 0.9.3
 **/
const gchar *
fwupd_device_get_version_lowest(FwupdDevice *self)
{
	FwupdDevicePrivate *priv = GET_PRIVATE(self);
	g_return_val_if_fail(FWUPD_IS_DEVICE(self), NULL);
	return priv->version_lowest;
}

/**
 * fwupd_device_set_version_lowest:
 * @self: a #FwupdDevice
 * @version_lowest: (nullable): the version
 *
 * Sets the lowest version of firmware the device will accept.
 *
 * Since: 0.9.3
 **/
void
fwupd_device_set_version_lowest(FwupdDevice *self, const gchar *version_lowest)
{
	FwupdDevicePrivate *priv = GET_PRIVATE(self);
	g_return_if_fail(FWUPD_IS_DEVICE(self));

	/* not changed */
	if (g_strcmp0(priv->version_lowest, version_lowest) == 0)
		return;

	g_free(priv->version_lowest);
	priv->version_lowest = g_strdup(version_lowest);
}

/**
 * fwupd_device_get_version_lowest_raw:
 * @self: a #FwupdDevice
 *
 * Gets the lowest version of firmware the device will accept in raw format.
 *
 * Returns: integer version number, or %0 if unset
 *
 * Since: 1.4.0
 **/
guint64
fwupd_device_get_version_lowest_raw(FwupdDevice *self)
{
	FwupdDevicePrivate *priv = GET_PRIVATE(self);
	g_return_val_if_fail(FWUPD_IS_DEVICE(self), 0);
	return priv->version_lowest_raw;
}

/**
 * fwupd_device_set_version_lowest_raw:
 * @self: a #FwupdDevice
 * @version_lowest_raw: the raw hardware version
 *
 * Sets the raw lowest version number from the hardware before converted to a string.
 *
 * Since: 1.4.0
 **/
void
fwupd_device_set_version_lowest_raw(FwupdDevice *self, guint64 version_lowest_raw)
{
	FwupdDevicePrivate *priv = GET_PRIVATE(self);
	g_return_if_fail(FWUPD_IS_DEVICE(self));
	priv->version_lowest_raw = version_lowest_raw;
}

/**
 * fwupd_device_get_version_bootloader:
 * @self: a #FwupdDevice
 *
 * Gets the version of the bootloader.
 *
 * Returns: the device version_bootloader, or %NULL if unset
 *
 * Since: 0.9.3
 **/
const gchar *
fwupd_device_get_version_bootloader(FwupdDevice *self)
{
	FwupdDevicePrivate *priv = GET_PRIVATE(self);
	g_return_val_if_fail(FWUPD_IS_DEVICE(self), NULL);
	return priv->version_bootloader;
}

/**
 * fwupd_device_set_version_bootloader:
 * @self: a #FwupdDevice
 * @version_bootloader: (nullable): the version
 *
 * Sets the bootloader version.
 *
 * Since: 0.9.3
 **/
void
fwupd_device_set_version_bootloader(FwupdDevice *self, const gchar *version_bootloader)
{
	FwupdDevicePrivate *priv = GET_PRIVATE(self);
	g_return_if_fail(FWUPD_IS_DEVICE(self));

	/* not changed */
	if (g_strcmp0(priv->version_bootloader, version_bootloader) == 0)
		return;

	g_free(priv->version_bootloader);
	priv->version_bootloader = g_strdup(version_bootloader);
}

/**
 * fwupd_device_get_version_bootloader_raw:
 * @self: a #FwupdDevice
 *
 * Gets the bootloader version of firmware the device will accept in raw format.
 *
 * Returns: integer version number, or %0 if unset
 *
 * Since: 1.4.0
 **/
guint64
fwupd_device_get_version_bootloader_raw(FwupdDevice *self)
{
	FwupdDevicePrivate *priv = GET_PRIVATE(self);
	g_return_val_if_fail(FWUPD_IS_DEVICE(self), 0);
	return priv->version_bootloader_raw;
}

/**
 * fwupd_device_set_version_bootloader_raw:
 * @self: a #FwupdDevice
 * @version_bootloader_raw: the raw hardware version
 *
 * Sets the raw bootloader version number from the hardware before converted to a string.
 *
 * Since: 1.4.0
 **/
void
fwupd_device_set_version_bootloader_raw(FwupdDevice *self, guint64 version_bootloader_raw)
{
	FwupdDevicePrivate *priv = GET_PRIVATE(self);
	g_return_if_fail(FWUPD_IS_DEVICE(self));
	priv->version_bootloader_raw = version_bootloader_raw;
}

/**
 * fwupd_device_get_flashes_left:
 * @self: a #FwupdDevice
 *
 * Gets the number of flash cycles left on the device
 *
 * Returns: the flash cycles left, or %NULL if unset
 *
 * Since: 0.9.3
 **/
guint32
fwupd_device_get_flashes_left(FwupdDevice *self)
{
	FwupdDevicePrivate *priv = GET_PRIVATE(self);
	g_return_val_if_fail(FWUPD_IS_DEVICE(self), 0);
	return priv->flashes_left;
}

/**
 * fwupd_device_set_flashes_left:
 * @self: a #FwupdDevice
 * @flashes_left: the description
 *
 * Sets the number of flash cycles left on the device
 *
 * Since: 0.9.3
 **/
void
fwupd_device_set_flashes_left(FwupdDevice *self, guint32 flashes_left)
{
	FwupdDevicePrivate *priv = GET_PRIVATE(self);
	g_return_if_fail(FWUPD_IS_DEVICE(self));
	priv->flashes_left = flashes_left;
}

/**
 * fwupd_device_get_battery_level:
 * @self: a #FwupdDevice
 *
 * Returns the battery level.
 *
 * Returns: value in percent
 *
 * Since: 1.8.1
 **/
guint32
fwupd_device_get_battery_level(FwupdDevice *self)
{
	FwupdDevicePrivate *priv = GET_PRIVATE(self);
	g_return_val_if_fail(FWUPD_IS_DEVICE(self), G_MAXUINT);
	return priv->battery_level;
}

/**
 * fwupd_device_set_battery_level:
 * @self: a #FwupdDevice
 * @battery_level: the percentage value
 *
 * Sets the battery level, or %FWUPD_BATTERY_LEVEL_INVALID.
 *
 * Setting this allows fwupd to show a warning if the device change is too low
 * to perform the update.
 *
 * Since: 1.8.1
 **/
void
fwupd_device_set_battery_level(FwupdDevice *self, guint32 battery_level)
{
	FwupdDevicePrivate *priv = GET_PRIVATE(self);

	g_return_if_fail(FWUPD_IS_DEVICE(self));
	g_return_if_fail(battery_level <= FWUPD_BATTERY_LEVEL_INVALID);

	if (priv->battery_level == battery_level)
		return;
	priv->battery_level = battery_level;
	g_object_notify(G_OBJECT(self), "battery-level");
}

/**
 * fwupd_device_get_battery_threshold:
 * @self: a #FwupdDevice
 *
 * Returns the battery threshold under which a firmware update cannot be
 * performed.
 *
 * If fwupd_device_set_battery_threshold() has not been used, a default value is
 * used instead.
 *
 * Returns: value in percent
 *
 * Since: 1.8.1
 **/
guint32
fwupd_device_get_battery_threshold(FwupdDevice *self)
{
	FwupdDevicePrivate *priv = GET_PRIVATE(self);

	g_return_val_if_fail(FWUPD_IS_DEVICE(self), FWUPD_BATTERY_LEVEL_INVALID);

	/* default value */
	if (priv->battery_threshold == FWUPD_BATTERY_LEVEL_INVALID)
		return FWUPD_BATTERY_THRESHOLD_DEFAULT;

	return priv->battery_threshold;
}

/**
 * fwupd_device_set_battery_threshold:
 * @self: a #FwupdDevice
 * @battery_threshold: the percentage value
 *
 * Sets the battery level, or %FWUPD_BATTERY_LEVEL_INVALID for the default.
 *
 * Setting this allows fwupd to show a warning if the device change is too low
 * to perform the update.
 *
 * Since: 1.8.1
 **/
void
fwupd_device_set_battery_threshold(FwupdDevice *self, guint32 battery_threshold)
{
	FwupdDevicePrivate *priv = GET_PRIVATE(self);

	g_return_if_fail(FWUPD_IS_DEVICE(self));
	g_return_if_fail(battery_threshold <= FWUPD_BATTERY_LEVEL_INVALID);

	if (priv->battery_threshold == battery_threshold)
		return;
	priv->battery_threshold = battery_threshold;
	g_object_notify(G_OBJECT(self), "battery-threshold");
}

/**
 * fwupd_device_get_install_duration:
 * @self: a #FwupdDevice
 *
 * Gets the time estimate for firmware installation (in seconds)
 *
 * Returns: the estimated time to flash this device (or 0 if unset)
 *
 * Since: 1.1.3
 **/
guint32
fwupd_device_get_install_duration(FwupdDevice *self)
{
	FwupdDevicePrivate *priv = GET_PRIVATE(self);
	g_return_val_if_fail(FWUPD_IS_DEVICE(self), 0);
	return priv->install_duration;
}

/**
 * fwupd_device_set_install_duration:
 * @self: a #FwupdDevice
 * @duration: the amount of time
 *
 * Sets the time estimate for firmware installation (in seconds)
 *
 * Since: 1.1.3
 **/
void
fwupd_device_set_install_duration(FwupdDevice *self, guint32 duration)
{
	FwupdDevicePrivate *priv = GET_PRIVATE(self);
	g_return_if_fail(FWUPD_IS_DEVICE(self));
	priv->install_duration = duration;
}

/**
 * fwupd_device_get_plugin:
 * @self: a #FwupdDevice
 *
 * Gets the plugin that created the device.
 *
 * Returns: the plugin name, or %NULL if unset
 *
 * Since: 1.0.0
 **/
const gchar *
fwupd_device_get_plugin(FwupdDevice *self)
{
	FwupdDevicePrivate *priv = GET_PRIVATE(self);
	g_return_val_if_fail(FWUPD_IS_DEVICE(self), NULL);
	return priv->plugin;
}

/**
 * fwupd_device_set_plugin:
 * @self: a #FwupdDevice
 * @plugin: (nullable): the plugin name, e.g. `colorhug`
 *
 * Sets the plugin that created the device.
 *
 * Since: 1.0.0
 **/
void
fwupd_device_set_plugin(FwupdDevice *self, const gchar *plugin)
{
	FwupdDevicePrivate *priv = GET_PRIVATE(self);
	g_return_if_fail(FWUPD_IS_DEVICE(self));

	/* not changed */
	if (g_strcmp0(priv->plugin, plugin) == 0)
		return;

	g_free(priv->plugin);
	priv->plugin = g_strdup(plugin);
}

static void
fwupd_device_ensure_protocols(FwupdDevice *self)
{
	FwupdDevicePrivate *priv = GET_PRIVATE(self);
	if (priv->protocols == NULL)
		priv->protocols = g_ptr_array_new_with_free_func(g_free);
}

/**
 * fwupd_device_get_protocols:
 * @self: a #FwupdDevice
 *
 * Gets the device protocol names.
 *
 * Returns: (element-type utf8) (transfer none): the device protocol names
 *
 * Since: 1.5.8
 **/
GPtrArray *
fwupd_device_get_protocols(FwupdDevice *self)
{
	FwupdDevicePrivate *priv = GET_PRIVATE(self);
	g_return_val_if_fail(FWUPD_IS_DEVICE(self), NULL);
	fwupd_device_ensure_protocols(self);
	return priv->protocols;
}

/**
 * fwupd_device_has_protocol:
 * @self: a #FwupdDevice
 * @protocol: (not nullable): the protocol name, e.g. `com.hughski.colorhug`
 *
 * Finds out if the device has this specific protocol name.
 *
 * Returns: %TRUE if the protocol name is found
 *
 * Since: 1.5.8
 **/
gboolean
fwupd_device_has_protocol(FwupdDevice *self, const gchar *protocol)
{
	FwupdDevicePrivate *priv = GET_PRIVATE(self);

	g_return_val_if_fail(FWUPD_IS_DEVICE(self), FALSE);
	g_return_val_if_fail(protocol != NULL, FALSE);

	if (priv->protocols == NULL)
		return FALSE;
	for (guint i = 0; i < priv->protocols->len; i++) {
		const gchar *protocol_tmp = g_ptr_array_index(priv->protocols, i);
		if (g_strcmp0(protocol, protocol_tmp) == 0)
			return TRUE;
	}
	return FALSE;
}

/**
 * fwupd_device_add_protocol:
 * @self: a #FwupdDevice
 * @protocol: (not nullable): the protocol name, e.g. `com.hughski.colorhug`
 *
 * Adds a device protocol name.
 *
 * Since: 1.5.8
 **/
void
fwupd_device_add_protocol(FwupdDevice *self, const gchar *protocol)
{
	FwupdDevicePrivate *priv = GET_PRIVATE(self);

	g_return_if_fail(FWUPD_IS_DEVICE(self));
	g_return_if_fail(protocol != NULL);

	if (fwupd_device_has_protocol(self, protocol))
		return;
	fwupd_device_ensure_protocols(self);
	g_ptr_array_add(priv->protocols, g_strdup(protocol));
}

/**
 * fwupd_device_get_flags:
 * @self: a #FwupdDevice
 *
 * Gets device flags.
 *
 * Returns: device flags, or 0 if unset
 *
 * Since: 0.9.3
 **/
guint64
fwupd_device_get_flags(FwupdDevice *self)
{
	FwupdDevicePrivate *priv = GET_PRIVATE(self);
	g_return_val_if_fail(FWUPD_IS_DEVICE(self), 0);
	return priv->flags;
}

/**
 * fwupd_device_set_flags:
 * @self: a #FwupdDevice
 * @flags: device flags, e.g. %FWUPD_DEVICE_FLAG_REQUIRE_AC
 *
 * Sets device flags.
 *
 * Since: 0.9.3
 **/
void
fwupd_device_set_flags(FwupdDevice *self, guint64 flags)
{
	FwupdDevicePrivate *priv = GET_PRIVATE(self);
	g_return_if_fail(FWUPD_IS_DEVICE(self));
	if (priv->flags == flags)
		return;
	priv->flags = flags;
	g_object_notify(G_OBJECT(self), "flags");
}

/**
 * fwupd_device_add_flag:
 * @self: a #FwupdDevice
 * @flag: the #FwupdDeviceFlags
 *
 * Adds a specific device flag to the device.
 *
 * Since: 0.9.3
 **/
void
fwupd_device_add_flag(FwupdDevice *self, FwupdDeviceFlags flag)
{
	FwupdDevicePrivate *priv = GET_PRIVATE(self);
	g_return_if_fail(FWUPD_IS_DEVICE(self));
	if (flag == 0)
		return;
	if ((priv->flags | flag) == priv->flags)
		return;
	priv->flags |= flag;
	g_object_notify(G_OBJECT(self), "flags");
}

/**
 * fwupd_device_remove_flag:
 * @self: a #FwupdDevice
 * @flag: the #FwupdDeviceFlags
 *
 * Removes a specific device flag from the device.
 *
 * Since: 0.9.3
 **/
void
fwupd_device_remove_flag(FwupdDevice *self, FwupdDeviceFlags flag)
{
	FwupdDevicePrivate *priv = GET_PRIVATE(self);
	g_return_if_fail(FWUPD_IS_DEVICE(self));
	if (flag == 0)
		return;
	if ((priv->flags & flag) == 0)
		return;
	priv->flags &= ~flag;
	g_object_notify(G_OBJECT(self), "flags");
}

/**
 * fwupd_device_has_flag:
 * @self: a #FwupdDevice
 * @flag: the #FwupdDeviceFlags
 *
 * Finds if the device has a specific device flag.
 *
 * Returns: %TRUE if the flag is set
 *
 * Since: 0.9.3
 **/
gboolean
fwupd_device_has_flag(FwupdDevice *self, FwupdDeviceFlags flag)
{
	FwupdDevicePrivate *priv = GET_PRIVATE(self);
	g_return_val_if_fail(FWUPD_IS_DEVICE(self), FALSE);
	return (priv->flags & flag) > 0;
}

/**
 * fwupd_device_get_problems:
 * @self: a #FwupdDevice
 *
 * Gets device problems.
 *
 * Returns: device problems, or 0 if unset
 *
 * Since: 1.8.1
 **/
guint64
fwupd_device_get_problems(FwupdDevice *self)
{
	FwupdDevicePrivate *priv = GET_PRIVATE(self);
	g_return_val_if_fail(FWUPD_IS_DEVICE(self), 0);
	return priv->problems;
}

/**
 * fwupd_device_set_problems:
 * @self: a #FwupdDevice
 * @problems: device problems, e.g. %FWUPD_DEVICE_PROBLEM_SYSTEM_POWER_TOO_LOW
 *
 * Sets device problems.
 *
 * Since: 1.8.1
 **/
void
fwupd_device_set_problems(FwupdDevice *self, guint64 problems)
{
	FwupdDevicePrivate *priv = GET_PRIVATE(self);
	g_return_if_fail(FWUPD_IS_DEVICE(self));
	if (priv->problems == problems)
		return;
	priv->problems = problems;
	g_object_notify(G_OBJECT(self), "problems");
}

/**
 * fwupd_device_add_problem:
 * @self: a #FwupdDevice
 * @problem: the #FwupdDeviceProblem, e.g. #FWUPD_DEVICE_PROBLEM_SYSTEM_POWER_TOO_LOW
 *
 * Adds a specific device problem kind to the device.
 *
 * Since: 1.8.1
 **/
void
fwupd_device_add_problem(FwupdDevice *self, FwupdDeviceProblem problem)
{
	FwupdDevicePrivate *priv = GET_PRIVATE(self);
	g_return_if_fail(FWUPD_IS_DEVICE(self));
	if (problem == FWUPD_DEVICE_PROBLEM_NONE)
		return;
	if (fwupd_device_has_problem(self, problem))
		return;
	priv->problems |= problem;
	g_object_notify(G_OBJECT(self), "problems");
}

/**
 * fwupd_device_remove_problem:
 * @self: a #FwupdDevice
 * @problem: the #FwupdDeviceProblem, e.g. #FWUPD_DEVICE_PROBLEM_SYSTEM_POWER_TOO_LOW
 *
 * Removes a specific device problem kind from the device.
 *
 * Since: 1.8.1
 **/
void
fwupd_device_remove_problem(FwupdDevice *self, FwupdDeviceProblem problem)
{
	FwupdDevicePrivate *priv = GET_PRIVATE(self);
	g_return_if_fail(FWUPD_IS_DEVICE(self));
	if (problem == FWUPD_DEVICE_PROBLEM_NONE)
		return;
	if (!fwupd_device_has_problem(self, problem))
		return;
	priv->problems &= ~problem;
	g_object_notify(G_OBJECT(self), "problems");
}

/**
 * fwupd_device_has_problem:
 * @self: a #FwupdDevice
 * @problem: the #FwupdDeviceProblem
 *
 * Finds if the device has a specific device problem kind.
 *
 * Returns: %TRUE if the problem is set
 *
 * Since: 1.8.1
 **/
gboolean
fwupd_device_has_problem(FwupdDevice *self, FwupdDeviceProblem problem)
{
	FwupdDevicePrivate *priv = GET_PRIVATE(self);
	g_return_val_if_fail(FWUPD_IS_DEVICE(self), FALSE);
	return (priv->problems & problem) > 0;
}

/**
 * fwupd_device_get_request_flags:
 * @self: a #FwupdDevice
 *
 * Gets device request flags.
 *
 * Returns: device request flags, or 0 if unset
 *
 * Since: 1.9.10
 **/
guint64
fwupd_device_get_request_flags(FwupdDevice *self)
{
	FwupdDevicePrivate *priv = GET_PRIVATE(self);
	g_return_val_if_fail(FWUPD_IS_DEVICE(self), 0);
	return priv->request_flags;
}

/**
 * fwupd_device_set_request_flags:
 * @self: a #FwupdDevice
 * @request_flags: device request flags, e.g. %FWUPD_DEVICE_REQUEST_FLAG_REQUIRE_AC
 *
 * Sets device request flags.
 *
 * Since: 1.9.10
 **/
void
fwupd_device_set_request_flags(FwupdDevice *self, guint64 request_flags)
{
	FwupdDevicePrivate *priv = GET_PRIVATE(self);
	g_return_if_fail(FWUPD_IS_DEVICE(self));
	if (priv->request_flags == request_flags)
		return;
	priv->request_flags = request_flags;
	g_object_notify(G_OBJECT(self), "request-flags");
}

/**
 * fwupd_device_add_request_flag:
 * @self: a #FwupdDevice
 * @request_flag: the #FwupdRequestFlags
 *
 * Adds a specific device request flag to the device.
 *
 * Since: 1.9.10
 **/
void
fwupd_device_add_request_flag(FwupdDevice *self, FwupdRequestFlags request_flag)
{
	FwupdDevicePrivate *priv = GET_PRIVATE(self);
	g_return_if_fail(FWUPD_IS_DEVICE(self));
	if (request_flag == 0)
		return;
	if ((priv->request_flags | request_flag) == priv->request_flags)
		return;
	priv->request_flags |= request_flag;
	g_object_notify(G_OBJECT(self), "request-flags");
}

/**
 * fwupd_device_remove_request_flag:
 * @self: a #FwupdDevice
 * @request_flag: the #FwupdRequestFlags
 *
 * Removes a specific device request flag from the device.
 *
 * Since: 1.9.10
 **/
void
fwupd_device_remove_request_flag(FwupdDevice *self, FwupdRequestFlags request_flag)
{
	FwupdDevicePrivate *priv = GET_PRIVATE(self);
	g_return_if_fail(FWUPD_IS_DEVICE(self));
	if (request_flag == 0)
		return;
	if ((priv->request_flags & request_flag) == 0)
		return;
	priv->request_flags &= ~request_flag;
	g_object_notify(G_OBJECT(self), "request-flags");
}

/**
 * fwupd_device_has_request_flag:
 * @self: a #FwupdDevice
 * @request_flag: the #FwupdRequestFlags
 *
 * Finds if the device has a specific device request flag.
 *
 * Returns: %TRUE if the request_flag is set
 *
 * Since: 1.9.10
 **/
gboolean
fwupd_device_has_request_flag(FwupdDevice *self, FwupdRequestFlags request_flag)
{
	FwupdDevicePrivate *priv = GET_PRIVATE(self);
	g_return_val_if_fail(FWUPD_IS_DEVICE(self), FALSE);
	return (priv->request_flags & request_flag) > 0;
}

/**
 * fwupd_device_get_created:
 * @self: a #FwupdDevice
 *
 * Gets when the device was created.
 *
 * Returns: the UNIX time, or 0 if unset
 *
 * Since: 0.9.3
 **/
guint64
fwupd_device_get_created(FwupdDevice *self)
{
	FwupdDevicePrivate *priv = GET_PRIVATE(self);
	g_return_val_if_fail(FWUPD_IS_DEVICE(self), 0);
	return priv->created;
}

/**
 * fwupd_device_set_created:
 * @self: a #FwupdDevice
 * @created: the UNIX time
 *
 * Sets when the device was created.
 *
 * Since: 0.9.3
 **/
void
fwupd_device_set_created(FwupdDevice *self, guint64 created)
{
	FwupdDevicePrivate *priv = GET_PRIVATE(self);
	g_return_if_fail(FWUPD_IS_DEVICE(self));
	priv->created = created;
}

/**
 * fwupd_device_get_modified:
 * @self: a #FwupdDevice
 *
 * Gets when the device was modified.
 *
 * Returns: the UNIX time, or 0 if unset
 *
 * Since: 0.9.3
 **/
guint64
fwupd_device_get_modified(FwupdDevice *self)
{
	FwupdDevicePrivate *priv = GET_PRIVATE(self);
	g_return_val_if_fail(FWUPD_IS_DEVICE(self), 0);
	return priv->modified;
}

/**
 * fwupd_device_set_modified:
 * @self: a #FwupdDevice
 * @modified: the UNIX time
 *
 * Sets when the device was modified.
 *
 * Since: 0.9.3
 **/
void
fwupd_device_set_modified(FwupdDevice *self, guint64 modified)
{
	FwupdDevicePrivate *priv = GET_PRIVATE(self);
	g_return_if_fail(FWUPD_IS_DEVICE(self));
	priv->modified = modified;
}

/**
 * fwupd_device_incorporate:
 * @self: a #FwupdDevice
 * @donor: Another #FwupdDevice
 *
 * Copy all properties from the donor object if they have not already been set.
 *
 * Since: 1.1.0
 **/
void
fwupd_device_incorporate(FwupdDevice *self, FwupdDevice *donor)
{
	FwupdDevicePrivate *priv = GET_PRIVATE(self);
	FwupdDevicePrivate *priv_donor = GET_PRIVATE(donor);

	g_return_if_fail(FWUPD_IS_DEVICE(self));
	g_return_if_fail(FWUPD_IS_DEVICE(donor));

	fwupd_device_add_flag(self, priv_donor->flags);
	fwupd_device_add_request_flag(self, priv_donor->request_flags);
	fwupd_device_add_problem(self, priv_donor->problems);
	if (priv->created == 0)
		fwupd_device_set_created(self, priv_donor->created);
	if (priv->modified == 0)
		fwupd_device_set_modified(self, priv_donor->modified);
	if (priv->version_build_date == 0)
		fwupd_device_set_version_build_date(self, priv_donor->version_build_date);
	if (priv->flashes_left == 0)
		fwupd_device_set_flashes_left(self, priv_donor->flashes_left);
	if (priv->battery_level == FWUPD_BATTERY_LEVEL_INVALID)
		fwupd_device_set_battery_level(self, priv_donor->battery_level);
	if (priv->battery_threshold == FWUPD_BATTERY_LEVEL_INVALID)
		fwupd_device_set_battery_threshold(self, priv_donor->battery_threshold);
	if (priv->install_duration == 0)
		fwupd_device_set_install_duration(self, priv_donor->install_duration);
	if (priv->update_state == FWUPD_UPDATE_STATE_UNKNOWN)
		fwupd_device_set_update_state(self, priv_donor->update_state);
	if (priv->id == NULL)
		fwupd_device_set_id(self, priv_donor->id);
	if (priv->parent_id == NULL)
		fwupd_device_set_parent_id(self, priv_donor->parent_id);
	if (priv->composite_id == NULL)
		fwupd_device_set_composite_id(self, priv_donor->composite_id);
	if (priv->name == NULL)
		fwupd_device_set_name(self, priv_donor->name);
	if (priv->serial == NULL)
		fwupd_device_set_serial(self, priv_donor->serial);
	if (priv->summary == NULL)
		fwupd_device_set_summary(self, priv_donor->summary);
	if (priv->branch == NULL)
		fwupd_device_set_branch(self, priv_donor->branch);
	if (priv->vendor == NULL)
		fwupd_device_set_vendor(self, priv_donor->vendor);
	if (priv_donor->vendor_ids != NULL) {
		for (guint i = 0; i < priv_donor->vendor_ids->len; i++) {
			const gchar *tmp = g_ptr_array_index(priv_donor->vendor_ids, i);
			fwupd_device_add_vendor_id(self, tmp);
		}
	}
	if (priv->plugin == NULL)
		fwupd_device_set_plugin(self, priv_donor->plugin);
	if (priv_donor->protocols != NULL) {
		for (guint i = 0; i < priv_donor->protocols->len; i++) {
			const gchar *tmp = g_ptr_array_index(priv_donor->protocols, i);
			fwupd_device_add_protocol(self, tmp);
		}
	}
	if (priv->update_error == NULL)
		fwupd_device_set_update_error(self, priv_donor->update_error);
	if (priv->version == NULL)
		fwupd_device_set_version(self, priv_donor->version);
	if (priv->version_lowest == NULL)
		fwupd_device_set_version_lowest(self, priv_donor->version_lowest);
	if (priv->version_bootloader == NULL)
		fwupd_device_set_version_bootloader(self, priv_donor->version_bootloader);
	if (priv->version_format == FWUPD_VERSION_FORMAT_UNKNOWN)
		fwupd_device_set_version_format(self, priv_donor->version_format);
	if (priv->version_raw == 0)
		fwupd_device_set_version_raw(self, priv_donor->version_raw);
	if (priv->version_lowest_raw == 0)
		fwupd_device_set_version_lowest_raw(self, priv_donor->version_lowest_raw);
	if (priv->version_bootloader_raw == 0)
		fwupd_device_set_version_bootloader_raw(self, priv_donor->version_bootloader_raw);
	if (priv_donor->guids != NULL) {
		for (guint i = 0; i < priv_donor->guids->len; i++) {
			const gchar *tmp = g_ptr_array_index(priv_donor->guids, i);
			fwupd_device_add_guid(self, tmp);
		}
	}
	if (priv_donor->instance_ids != NULL) {
		for (guint i = 0; i < priv_donor->instance_ids->len; i++) {
			const gchar *tmp = g_ptr_array_index(priv_donor->instance_ids, i);
			fwupd_device_add_instance_id(self, tmp);
		}
	}
	if (priv_donor->icons != NULL) {
		for (guint i = 0; i < priv_donor->icons->len; i++) {
			const gchar *tmp = g_ptr_array_index(priv_donor->icons, i);
			fwupd_device_add_icon(self, tmp);
		}
	}
	if (priv_donor->checksums != NULL) {
		for (guint i = 0; i < priv_donor->checksums->len; i++) {
			const gchar *tmp = g_ptr_array_index(priv_donor->checksums, i);
			fwupd_device_add_checksum(self, tmp);
		}
	}
	if (priv_donor->releases != NULL) {
		for (guint i = 0; i < priv_donor->releases->len; i++) {
			FwupdRelease *tmp = g_ptr_array_index(priv_donor->releases, i);
			fwupd_device_add_release(self, tmp);
		}
	}
}

static void
fwupd_device_add_variant(FwupdCodec *codec, GVariantBuilder *builder, FwupdCodecFlags flags)
{
	FwupdDevice *self = FWUPD_DEVICE(codec);
	FwupdDevicePrivate *priv = GET_PRIVATE(self);

	if (priv->id != NULL) {
		g_variant_builder_add(builder,
				      "{sv}",
				      FWUPD_RESULT_KEY_DEVICE_ID,
				      g_variant_new_string(priv->id));
	}
	if (priv->parent_id != NULL) {
		g_variant_builder_add(builder,
				      "{sv}",
				      FWUPD_RESULT_KEY_PARENT_DEVICE_ID,
				      g_variant_new_string(priv->parent_id));
	}
	if (priv->composite_id != NULL) {
		g_variant_builder_add(builder,
				      "{sv}",
				      FWUPD_RESULT_KEY_COMPOSITE_ID,
				      g_variant_new_string(priv->composite_id));
	}
	if (priv->guids != NULL && priv->guids->len > 0) {
		const gchar *const *tmp = (const gchar *const *)priv->guids->pdata;
		g_variant_builder_add(builder,
				      "{sv}",
				      FWUPD_RESULT_KEY_GUID,
				      g_variant_new_strv(tmp, priv->guids->len));
	}
	if (priv->icons != NULL && priv->icons->len > 0) {
		const gchar *const *tmp = (const gchar *const *)priv->icons->pdata;
		g_variant_builder_add(builder,
				      "{sv}",
				      FWUPD_RESULT_KEY_ICON,
				      g_variant_new_strv(tmp, priv->icons->len));
	}
	if (priv->name != NULL) {
		g_variant_builder_add(builder,
				      "{sv}",
				      FWUPD_RESULT_KEY_NAME,
				      g_variant_new_string(priv->name));
	}
	if (priv->vendor != NULL) {
		g_variant_builder_add(builder,
				      "{sv}",
				      FWUPD_RESULT_KEY_VENDOR,
				      g_variant_new_string(priv->vendor));
	}
	if (priv->vendor_ids != NULL && priv->vendor_ids->len > 0) {
		g_autoptr(GString) str = g_string_new(NULL);
		for (guint i = 0; i < priv->vendor_ids->len; i++) {
			const gchar *tmp = g_ptr_array_index(priv->vendor_ids, i);
			g_string_append_printf(str, "%s|", tmp);
		}
		if (str->len > 0)
			g_string_truncate(str, str->len - 1);
		g_variant_builder_add(builder,
				      "{sv}",
				      FWUPD_RESULT_KEY_VENDOR_ID,
				      g_variant_new_string(str->str));
	}
	if (priv->flags > 0) {
		g_variant_builder_add(builder,
				      "{sv}",
				      FWUPD_RESULT_KEY_FLAGS,
				      g_variant_new_uint64(priv->flags));
	}
	if (priv->request_flags > 0) {
		g_variant_builder_add(builder,
				      "{sv}",
				      FWUPD_RESULT_KEY_REQUEST_FLAGS,
				      g_variant_new_uint64(priv->request_flags));
	}
	if (priv->problems > 0) {
		g_variant_builder_add(builder,
				      "{sv}",
				      FWUPD_RESULT_KEY_PROBLEMS,
				      g_variant_new_uint64(priv->problems));
	}
	if (priv->created > 0) {
		g_variant_builder_add(builder,
				      "{sv}",
				      FWUPD_RESULT_KEY_CREATED,
				      g_variant_new_uint64(priv->created));
	}
	if (priv->modified > 0) {
		g_variant_builder_add(builder,
				      "{sv}",
				      FWUPD_RESULT_KEY_MODIFIED,
				      g_variant_new_uint64(priv->modified));
	}
	if (priv->version_build_date > 0) {
		g_variant_builder_add(builder,
				      "{sv}",
				      FWUPD_RESULT_KEY_VERSION_BUILD_DATE,
				      g_variant_new_uint64(priv->version_build_date));
	}

	if (priv->summary != NULL) {
		g_variant_builder_add(builder,
				      "{sv}",
				      FWUPD_RESULT_KEY_SUMMARY,
				      g_variant_new_string(priv->summary));
	}
	if (priv->branch != NULL) {
		g_variant_builder_add(builder,
				      "{sv}",
				      FWUPD_RESULT_KEY_BRANCH,
				      g_variant_new_string(priv->branch));
	}
	if (priv->checksums != NULL && priv->checksums->len > 0) {
		g_autoptr(GString) str = g_string_new("");
		for (guint i = 0; i < priv->checksums->len; i++) {
			const gchar *checksum = g_ptr_array_index(priv->checksums, i);
			g_string_append_printf(str, "%s,", checksum);
		}
		if (str->len > 0)
			g_string_truncate(str, str->len - 1);
		g_variant_builder_add(builder,
				      "{sv}",
				      FWUPD_RESULT_KEY_CHECKSUM,
				      g_variant_new_string(str->str));
	}
	if (priv->plugin != NULL) {
		g_variant_builder_add(builder,
				      "{sv}",
				      FWUPD_RESULT_KEY_PLUGIN,
				      g_variant_new_string(priv->plugin));
	}
	if (priv->protocols != NULL && priv->protocols->len > 0) {
		g_autoptr(GString) str = g_string_new(NULL);
		for (guint i = 0; i < priv->protocols->len; i++) {
			const gchar *tmp = g_ptr_array_index(priv->protocols, i);
			g_string_append_printf(str, "%s|", tmp);
		}
		if (str->len > 0)
			g_string_truncate(str, str->len - 1);
		g_variant_builder_add(builder,
				      "{sv}",
				      FWUPD_RESULT_KEY_PROTOCOL,
				      g_variant_new_string(str->str));
	}
	if (priv->issues != NULL && priv->issues->len > 0) {
		g_autofree const gchar **strv = g_new0(const gchar *, priv->issues->len + 1);
		for (guint i = 0; i < priv->issues->len; i++)
			strv[i] = (const gchar *)g_ptr_array_index(priv->issues, i);
		g_variant_builder_add(builder,
				      "{sv}",
				      FWUPD_RESULT_KEY_ISSUES,
				      g_variant_new_strv(strv, -1));
	}
	if (priv->version != NULL) {
		g_variant_builder_add(builder,
				      "{sv}",
				      FWUPD_RESULT_KEY_VERSION,
				      g_variant_new_string(priv->version));
	}
	if (priv->version_lowest != NULL) {
		g_variant_builder_add(builder,
				      "{sv}",
				      FWUPD_RESULT_KEY_VERSION_LOWEST,
				      g_variant_new_string(priv->version_lowest));
	}
	if (priv->version_bootloader != NULL) {
		g_variant_builder_add(builder,
				      "{sv}",
				      FWUPD_RESULT_KEY_VERSION_BOOTLOADER,
				      g_variant_new_string(priv->version_bootloader));
	}
	if (priv->version_raw > 0) {
		g_variant_builder_add(builder,
				      "{sv}",
				      FWUPD_RESULT_KEY_VERSION_RAW,
				      g_variant_new_uint64(priv->version_raw));
	}
	if (priv->version_lowest_raw > 0) {
		g_variant_builder_add(builder,
				      "{sv}",
				      FWUPD_RESULT_KEY_VERSION_LOWEST_RAW,
				      g_variant_new_uint64(priv->version_lowest_raw));
	}
	if (priv->version_bootloader_raw > 0) {
		g_variant_builder_add(builder,
				      "{sv}",
				      FWUPD_RESULT_KEY_VERSION_BOOTLOADER_RAW,
				      g_variant_new_uint64(priv->version_raw));
	}
	if (priv->flashes_left > 0) {
		g_variant_builder_add(builder,
				      "{sv}",
				      FWUPD_RESULT_KEY_FLASHES_LEFT,
				      g_variant_new_uint32(priv->flashes_left));
	}
	if (priv->battery_level != FWUPD_BATTERY_LEVEL_INVALID) {
		g_variant_builder_add(builder,
				      "{sv}",
				      FWUPD_RESULT_KEY_BATTERY_LEVEL,
				      g_variant_new_uint32(priv->battery_level));
	}
	if (priv->battery_threshold != FWUPD_BATTERY_LEVEL_INVALID) {
		g_variant_builder_add(builder,
				      "{sv}",
				      FWUPD_RESULT_KEY_BATTERY_THRESHOLD,
				      g_variant_new_uint32(priv->battery_threshold));
	}
	if (priv->install_duration > 0) {
		g_variant_builder_add(builder,
				      "{sv}",
				      FWUPD_RESULT_KEY_INSTALL_DURATION,
				      g_variant_new_uint32(priv->install_duration));
	}
	if (priv->update_error != NULL) {
		g_variant_builder_add(builder,
				      "{sv}",
				      FWUPD_RESULT_KEY_UPDATE_ERROR,
				      g_variant_new_string(priv->update_error));
	}
	if (priv->update_state != FWUPD_UPDATE_STATE_UNKNOWN) {
		g_variant_builder_add(builder,
				      "{sv}",
				      FWUPD_RESULT_KEY_UPDATE_STATE,
				      g_variant_new_uint32(priv->update_state));
	}
	if (priv->status != FWUPD_STATUS_UNKNOWN) {
		g_variant_builder_add(builder,
				      "{sv}",
				      FWUPD_RESULT_KEY_STATUS,
				      g_variant_new_uint32(priv->status));
	}
	if (priv->percentage != 0) {
		g_variant_builder_add(builder,
				      "{sv}",
				      FWUPD_RESULT_KEY_PERCENTAGE,
				      g_variant_new_uint32(priv->percentage));
	}
	if (priv->version_format != FWUPD_VERSION_FORMAT_UNKNOWN) {
		g_variant_builder_add(builder,
				      "{sv}",
				      FWUPD_RESULT_KEY_VERSION_FORMAT,
				      g_variant_new_uint32(priv->version_format));
	}
	if (priv->instance_ids != NULL && (flags & FWUPD_CODEC_FLAG_TRUSTED) > 0) {
		if (priv->serial != NULL) {
			g_variant_builder_add(builder,
					      "{sv}",
					      FWUPD_RESULT_KEY_SERIAL,
					      g_variant_new_string(priv->serial));
		}
		if (priv->instance_ids->len > 0) {
			const gchar *const *tmp = (const gchar *const *)priv->instance_ids->pdata;
			g_variant_builder_add(builder,
					      "{sv}",
					      FWUPD_RESULT_KEY_INSTANCE_IDS,
					      g_variant_new_strv(tmp, priv->instance_ids->len));
		}
	}

	/* create an array with all the metadata in */
	if (priv->releases != NULL && priv->releases->len > 0) {
		g_autofree GVariant **children = NULL;
		children = g_new0(GVariant *, priv->releases->len);
		for (guint i = 0; i < priv->releases->len; i++) {
			FwupdRelease *release = g_ptr_array_index(priv->releases, i);
			children[i] =
			    fwupd_codec_to_variant(FWUPD_CODEC(release), FWUPD_CODEC_FLAG_NONE);
		}
		g_variant_builder_add(
		    builder,
		    "{sv}",
		    FWUPD_RESULT_KEY_RELEASE,
		    g_variant_new_array(G_VARIANT_TYPE("a{sv}"), children, priv->releases->len));
	}
}

static void
fwupd_device_from_key_value(FwupdDevice *self, const gchar *key, GVariant *value)
{
	if (g_strcmp0(key, FWUPD_RESULT_KEY_RELEASE) == 0) {
		GVariantIter iter;
		GVariant *child;
		g_variant_iter_init(&iter, value);
		while ((child = g_variant_iter_next_value(&iter))) {
			g_autoptr(FwupdRelease) release = fwupd_release_new();
			if (fwupd_codec_from_variant(FWUPD_CODEC(release), child, NULL))
				fwupd_device_add_release(self, release);
			g_variant_unref(child);
		}
		return;
	}
	if (g_strcmp0(key, FWUPD_RESULT_KEY_DEVICE_ID) == 0) {
		fwupd_device_set_id(self, g_variant_get_string(value, NULL));
		return;
	}
	if (g_strcmp0(key, FWUPD_RESULT_KEY_PARENT_DEVICE_ID) == 0) {
		fwupd_device_set_parent_id(self, g_variant_get_string(value, NULL));
		return;
	}
	if (g_strcmp0(key, FWUPD_RESULT_KEY_COMPOSITE_ID) == 0) {
		fwupd_device_set_composite_id(self, g_variant_get_string(value, NULL));
		return;
	}
	if (g_strcmp0(key, FWUPD_RESULT_KEY_FLAGS) == 0) {
		fwupd_device_set_flags(self, g_variant_get_uint64(value));
		return;
	}
	if (g_strcmp0(key, FWUPD_RESULT_KEY_PROBLEMS) == 0) {
		fwupd_device_set_problems(self, g_variant_get_uint64(value));
		return;
	}
	if (g_strcmp0(key, FWUPD_RESULT_KEY_REQUEST_FLAGS) == 0) {
		fwupd_device_set_request_flags(self, g_variant_get_uint64(value));
		return;
	}
	if (g_strcmp0(key, FWUPD_RESULT_KEY_CREATED) == 0) {
		fwupd_device_set_created(self, g_variant_get_uint64(value));
		return;
	}
	if (g_strcmp0(key, FWUPD_RESULT_KEY_MODIFIED) == 0) {
		fwupd_device_set_modified(self, g_variant_get_uint64(value));
		return;
	}
	if (g_strcmp0(key, FWUPD_RESULT_KEY_VERSION_BUILD_DATE) == 0) {
		fwupd_device_set_version_build_date(self, g_variant_get_uint64(value));
		return;
	}
	if (g_strcmp0(key, FWUPD_RESULT_KEY_GUID) == 0) {
		g_autofree const gchar **guids = g_variant_get_strv(value, NULL);
		for (guint i = 0; guids != NULL && guids[i] != NULL; i++)
			fwupd_device_add_guid(self, guids[i]);
		return;
	}
	if (g_strcmp0(key, FWUPD_RESULT_KEY_INSTANCE_IDS) == 0) {
		g_autofree const gchar **instance_ids = g_variant_get_strv(value, NULL);
		for (guint i = 0; instance_ids != NULL && instance_ids[i] != NULL; i++)
			fwupd_device_add_instance_id(self, instance_ids[i]);
		return;
	}
	if (g_strcmp0(key, FWUPD_RESULT_KEY_ICON) == 0) {
		g_autofree const gchar **icons = g_variant_get_strv(value, NULL);
		for (guint i = 0; icons != NULL && icons[i] != NULL; i++)
			fwupd_device_add_icon(self, icons[i]);
		return;
	}
	if (g_strcmp0(key, FWUPD_RESULT_KEY_NAME) == 0) {
		fwupd_device_set_name(self, g_variant_get_string(value, NULL));
		return;
	}
	if (g_strcmp0(key, FWUPD_RESULT_KEY_VENDOR) == 0) {
		fwupd_device_set_vendor(self, g_variant_get_string(value, NULL));
		return;
	}
	if (g_strcmp0(key, FWUPD_RESULT_KEY_VENDOR_ID) == 0) {
		g_auto(GStrv) vendor_ids = NULL;
		vendor_ids = g_strsplit(g_variant_get_string(value, NULL), "|", -1);
		for (guint i = 0; vendor_ids[i] != NULL; i++)
			fwupd_device_add_vendor_id(self, vendor_ids[i]);
		return;
	}
	if (g_strcmp0(key, FWUPD_RESULT_KEY_SERIAL) == 0) {
		fwupd_device_set_serial(self, g_variant_get_string(value, NULL));
		return;
	}
	if (g_strcmp0(key, FWUPD_RESULT_KEY_SUMMARY) == 0) {
		fwupd_device_set_summary(self, g_variant_get_string(value, NULL));
		return;
	}
	if (g_strcmp0(key, FWUPD_RESULT_KEY_BRANCH) == 0) {
		fwupd_device_set_branch(self, g_variant_get_string(value, NULL));
		return;
	}
	if (g_strcmp0(key, FWUPD_RESULT_KEY_CHECKSUM) == 0) {
		const gchar *checksums = g_variant_get_string(value, NULL);
		if (checksums != NULL) {
			g_auto(GStrv) split = g_strsplit(checksums, ",", -1);
			for (guint i = 0; split[i] != NULL; i++)
				fwupd_device_add_checksum(self, split[i]);
		}
		return;
	}
	if (g_strcmp0(key, FWUPD_RESULT_KEY_PLUGIN) == 0) {
		fwupd_device_set_plugin(self, g_variant_get_string(value, NULL));
		return;
	}
	if (g_strcmp0(key, FWUPD_RESULT_KEY_PROTOCOL) == 0) {
		g_auto(GStrv) protocols = NULL;
		protocols = g_strsplit(g_variant_get_string(value, NULL), "|", -1);
		for (guint i = 0; protocols[i] != NULL; i++)
			fwupd_device_add_protocol(self, protocols[i]);
		return;
	}
	if (g_strcmp0(key, FWUPD_RESULT_KEY_ISSUES) == 0) {
		g_autofree const gchar **strv = g_variant_get_strv(value, NULL);
		for (guint i = 0; strv[i] != NULL; i++)
			fwupd_device_add_issue(self, strv[i]);
		return;
	}
	if (g_strcmp0(key, FWUPD_RESULT_KEY_VERSION) == 0) {
		fwupd_device_set_version(self, g_variant_get_string(value, NULL));
		return;
	}
	if (g_strcmp0(key, FWUPD_RESULT_KEY_VERSION_LOWEST) == 0) {
		fwupd_device_set_version_lowest(self, g_variant_get_string(value, NULL));
		return;
	}
	if (g_strcmp0(key, FWUPD_RESULT_KEY_VERSION_BOOTLOADER) == 0) {
		fwupd_device_set_version_bootloader(self, g_variant_get_string(value, NULL));
		return;
	}
	if (g_strcmp0(key, FWUPD_RESULT_KEY_FLASHES_LEFT) == 0) {
		fwupd_device_set_flashes_left(self, g_variant_get_uint32(value));
		return;
	}
	if (g_strcmp0(key, FWUPD_RESULT_KEY_BATTERY_LEVEL) == 0) {
		fwupd_device_set_battery_level(self, g_variant_get_uint32(value));
		return;
	}
	if (g_strcmp0(key, FWUPD_RESULT_KEY_BATTERY_THRESHOLD) == 0) {
		fwupd_device_set_battery_threshold(self, g_variant_get_uint32(value));
		return;
	}
	if (g_strcmp0(key, FWUPD_RESULT_KEY_INSTALL_DURATION) == 0) {
		fwupd_device_set_install_duration(self, g_variant_get_uint32(value));
		return;
	}
	if (g_strcmp0(key, FWUPD_RESULT_KEY_UPDATE_ERROR) == 0) {
		fwupd_device_set_update_error(self, g_variant_get_string(value, NULL));
		return;
	}
	if (g_strcmp0(key, FWUPD_RESULT_KEY_UPDATE_STATE) == 0) {
		fwupd_device_set_update_state(self, g_variant_get_uint32(value));
		return;
	}
	if (g_strcmp0(key, FWUPD_RESULT_KEY_STATUS) == 0) {
		fwupd_device_set_status(self, g_variant_get_uint32(value));
		return;
	}
	if (g_strcmp0(key, FWUPD_RESULT_KEY_PERCENTAGE) == 0) {
		fwupd_device_set_percentage(self, g_variant_get_uint32(value));
		return;
	}
	if (g_strcmp0(key, FWUPD_RESULT_KEY_VERSION_FORMAT) == 0) {
		fwupd_device_set_version_format(self, g_variant_get_uint32(value));
		return;
	}
	if (g_strcmp0(key, FWUPD_RESULT_KEY_VERSION_RAW) == 0) {
		fwupd_device_set_version_raw(self, g_variant_get_uint64(value));
		return;
	}
	if (g_strcmp0(key, FWUPD_RESULT_KEY_VERSION_LOWEST_RAW) == 0) {
		fwupd_device_set_version_lowest_raw(self, g_variant_get_uint64(value));
		return;
	}
	if (g_strcmp0(key, FWUPD_RESULT_KEY_VERSION_BOOTLOADER_RAW) == 0) {
		fwupd_device_set_version_bootloader_raw(self, g_variant_get_uint64(value));
		return;
	}
}

static void
fwupd_device_string_append_flags(GString *str, guint idt, const gchar *key, guint64 device_flags)
{
	g_autoptr(GString) tmp = g_string_new("");
	for (guint i = 0; i < 64; i++) {
		if ((device_flags & ((guint64)1 << i)) == 0)
			continue;
		g_string_append_printf(tmp, "%s|", fwupd_device_flag_to_string((guint64)1 << i));
	}
	if (tmp->len == 0) {
		g_string_append(tmp, fwupd_device_flag_to_string(0));
	} else {
		g_string_truncate(tmp, tmp->len - 1);
	}
	fwupd_codec_string_append(str, idt, key, tmp->str);
}

static void
fwupd_device_string_append_request_flags(GString *str,
					 guint idt,
					 const gchar *key,
					 guint64 request_flags)
{
	g_autoptr(GString) tmp = g_string_new("");
	for (guint i = 0; i < 64; i++) {
		if ((request_flags & ((guint64)1 << i)) == 0)
			continue;
		g_string_append_printf(tmp, "%s|", fwupd_request_flag_to_string((guint64)1 << i));
	}
	if (tmp->len == 0) {
		g_string_append(tmp, fwupd_request_flag_to_string(0));
	} else {
		g_string_truncate(tmp, tmp->len - 1);
	}
	fwupd_codec_string_append(str, idt, key, tmp->str);
}

static void
fwupd_device_string_append_problems(GString *str,
				    guint idt,
				    const gchar *key,
				    guint64 device_problems)
{
	g_autoptr(GString) tmp = g_string_new("");
	for (guint i = 0; i < 64; i++) {
		if ((device_problems & ((guint64)1 << i)) == 0)
			continue;
		g_string_append_printf(tmp, "%s|", fwupd_device_problem_to_string((guint64)1 << i));
	}
	if (tmp->len == 0) {
		g_string_append(tmp, fwupd_device_problem_to_string(0));
	} else {
		g_string_truncate(tmp, tmp->len - 1);
	}
	fwupd_codec_string_append(str, idt, key, tmp->str);
}

/**
 * fwupd_device_get_update_state:
 * @self: a #FwupdDevice
 *
 * Gets the update state.
 *
 * Returns: the update state, or %FWUPD_UPDATE_STATE_UNKNOWN if unset
 *
 * Since: 0.9.8
 **/
FwupdUpdateState
fwupd_device_get_update_state(FwupdDevice *self)
{
	FwupdDevicePrivate *priv = GET_PRIVATE(self);
	g_return_val_if_fail(FWUPD_IS_DEVICE(self), FWUPD_UPDATE_STATE_UNKNOWN);
	return priv->update_state;
}

/**
 * fwupd_device_set_update_state:
 * @self: a #FwupdDevice
 * @update_state: the state, e.g. %FWUPD_UPDATE_STATE_PENDING
 *
 * Sets the update state.
 *
 * Since: 0.9.8
 **/
void
fwupd_device_set_update_state(FwupdDevice *self, FwupdUpdateState update_state)
{
	FwupdDevicePrivate *priv = GET_PRIVATE(self);
	g_return_if_fail(FWUPD_IS_DEVICE(self));
	if (priv->update_state == update_state)
		return;
	priv->update_state = update_state;
	g_object_notify(G_OBJECT(self), "update-state");
}

/**
 * fwupd_device_get_version_format:
 * @self: a #FwupdDevice
 *
 * Gets the version format.
 *
 * Returns: the version format, or %FWUPD_VERSION_FORMAT_UNKNOWN if unset
 *
 * Since: 1.2.9
 **/
FwupdVersionFormat
fwupd_device_get_version_format(FwupdDevice *self)
{
	FwupdDevicePrivate *priv = GET_PRIVATE(self);
	g_return_val_if_fail(FWUPD_IS_DEVICE(self), FWUPD_VERSION_FORMAT_UNKNOWN);
	return priv->version_format;
}

/**
 * fwupd_device_set_version_format:
 * @self: a #FwupdDevice
 * @version_format: the version format, e.g. %FWUPD_VERSION_FORMAT_NUMBER
 *
 * Sets the version format.
 *
 * Since: 1.2.9
 **/
void
fwupd_device_set_version_format(FwupdDevice *self, FwupdVersionFormat version_format)
{
	FwupdDevicePrivate *priv = GET_PRIVATE(self);
	g_return_if_fail(FWUPD_IS_DEVICE(self));
	priv->version_format = version_format;
}

/**
 * fwupd_device_get_version_raw:
 * @self: a #FwupdDevice
 *
 * Gets the raw version number from the hardware before converted to a string.
 *
 * Returns: the hardware version, or 0 if unset
 *
 * Since: 1.3.6
 **/
guint64
fwupd_device_get_version_raw(FwupdDevice *self)
{
	FwupdDevicePrivate *priv = GET_PRIVATE(self);
	g_return_val_if_fail(FWUPD_IS_DEVICE(self), 0);
	return priv->version_raw;
}

/**
 * fwupd_device_set_version_raw:
 * @self: a #FwupdDevice
 * @version_raw: the raw hardware version
 *
 * Sets the raw version number from the hardware before converted to a string.
 *
 * Since: 1.3.6
 **/
void
fwupd_device_set_version_raw(FwupdDevice *self, guint64 version_raw)
{
	FwupdDevicePrivate *priv = GET_PRIVATE(self);
	g_return_if_fail(FWUPD_IS_DEVICE(self));
	priv->version_raw = version_raw;
}

/**
 * fwupd_device_get_version_build_date:
 * @self: a #FwupdDevice
 *
 * Gets the date when the firmware was built.
 *
 * Returns: the UNIX time, or 0 if unset
 *
 * Since: 1.6.2
 **/
guint64
fwupd_device_get_version_build_date(FwupdDevice *self)
{
	FwupdDevicePrivate *priv = GET_PRIVATE(self);
	g_return_val_if_fail(FWUPD_IS_DEVICE(self), 0);
	return priv->version_build_date;
}

/**
 * fwupd_device_set_version_build_date:
 * @self: a #FwupdDevice
 * @version_build_date: the UNIX time
 *
 * Sets the date when the firmware was built.
 *
 * Since: 1.6.2
 **/
void
fwupd_device_set_version_build_date(FwupdDevice *self, guint64 version_build_date)
{
	FwupdDevicePrivate *priv = GET_PRIVATE(self);
	g_return_if_fail(FWUPD_IS_DEVICE(self));
	priv->version_build_date = version_build_date;
}

/**
 * fwupd_device_get_update_error:
 * @self: a #FwupdDevice
 *
 * Gets the update error string.
 *
 * Returns: the update error string, or %NULL if unset
 *
 * Since: 0.9.8
 **/
const gchar *
fwupd_device_get_update_error(FwupdDevice *self)
{
	FwupdDevicePrivate *priv = GET_PRIVATE(self);
	g_return_val_if_fail(FWUPD_IS_DEVICE(self), NULL);
	return priv->update_error;
}

/**
 * fwupd_device_set_update_error:
 * @self: a #FwupdDevice
 * @update_error: (nullable): the update error string
 *
 * Sets the update error string.
 *
 * Since: 0.9.8
 **/
void
fwupd_device_set_update_error(FwupdDevice *self, const gchar *update_error)
{
	FwupdDevicePrivate *priv = GET_PRIVATE(self);
	g_return_if_fail(FWUPD_IS_DEVICE(self));

	/* not changed */
	if (g_strcmp0(priv->update_error, update_error) == 0)
		return;

	g_free(priv->update_error);
	priv->update_error = g_strdup(update_error);
	g_object_notify(G_OBJECT(self), "update-error");
}

/**
 * fwupd_device_get_release_default:
 * @self: a #FwupdDevice
 *
 * Gets the default release for this device.
 *
 * Returns: (transfer none): the #FwupdRelease, or %NULL if not set
 *
 * Since: 0.9.8
 **/
FwupdRelease *
fwupd_device_get_release_default(FwupdDevice *self)
{
	FwupdDevicePrivate *priv = GET_PRIVATE(self);
	g_return_val_if_fail(FWUPD_IS_DEVICE(self), NULL);
	if (priv->releases == NULL || priv->releases->len == 0)
		return NULL;
	return FWUPD_RELEASE(g_ptr_array_index(priv->releases, 0));
}

static void
fwupd_device_ensure_releases(FwupdDevice *self)
{
	FwupdDevicePrivate *priv = GET_PRIVATE(self);
	if (priv->releases == NULL)
		priv->releases = g_ptr_array_new_with_free_func((GDestroyNotify)g_object_unref);
}

/**
 * fwupd_device_get_releases:
 * @self: a #FwupdDevice
 *
 * Gets all the releases for this device.
 *
 * Returns: (transfer none) (element-type FwupdRelease): array of releases
 *
 * Since: 0.9.8
 **/
GPtrArray *
fwupd_device_get_releases(FwupdDevice *self)
{
	FwupdDevicePrivate *priv = GET_PRIVATE(self);
	g_return_val_if_fail(FWUPD_IS_DEVICE(self), NULL);
	fwupd_device_ensure_releases(self);
	return priv->releases;
}

/**
 * fwupd_device_add_release:
 * @self: a #FwupdDevice
 * @release: (not nullable): a release
 *
 * Adds a release for this device.
 *
 * Since: 0.9.8
 **/
void
fwupd_device_add_release(FwupdDevice *self, FwupdRelease *release)
{
	FwupdDevicePrivate *priv = GET_PRIVATE(self);
	g_return_if_fail(FWUPD_IS_DEVICE(self));
	g_return_if_fail(FWUPD_IS_RELEASE(release));
	fwupd_device_ensure_releases(self);
	g_ptr_array_add(priv->releases, g_object_ref(release));
}

/**
 * fwupd_device_get_status:
 * @self: a #FwupdDevice
 *
 * Returns what the device is currently doing.
 *
 * Returns: the status value, e.g. %FWUPD_STATUS_DEVICE_WRITE
 *
 * Since: 1.4.0
 **/
FwupdStatus
fwupd_device_get_status(FwupdDevice *self)
{
	FwupdDevicePrivate *priv = GET_PRIVATE(self);
	g_return_val_if_fail(FWUPD_IS_DEVICE(self), 0);
	return priv->status;
}

/**
 * fwupd_device_set_status:
 * @self: a #FwupdDevice
 * @status: the status value, e.g. %FWUPD_STATUS_DEVICE_WRITE
 *
 * Sets what the device is currently doing.
 *
 * Since: 1.4.0
 **/
void
fwupd_device_set_status(FwupdDevice *self, FwupdStatus status)
{
	FwupdDevicePrivate *priv = GET_PRIVATE(self);
	g_return_if_fail(FWUPD_IS_DEVICE(self));
	if (priv->status == status)
		return;
	priv->status = status;
	g_object_notify(G_OBJECT(self), "status");
}

/**
 * fwupd_device_get_percentage:
 * @self: a #FwupdDevice
 *
 * Returns the percentage completion of the device.
 *
 * Returns: the percentage value
 *
 * Since: 1.8.11
 **/
guint
fwupd_device_get_percentage(FwupdDevice *self)
{
	FwupdDevicePrivate *priv = GET_PRIVATE(self);
	g_return_val_if_fail(FWUPD_IS_DEVICE(self), 0);
	return priv->percentage;
}

/**
 * fwupd_device_set_percentage:
 * @self: a #FwupdDevice
 * @percentage: the percentage value
 *
 * Sets the percentage completion of the device.
 *
 * Since: 1.8.11
 **/
void
fwupd_device_set_percentage(FwupdDevice *self, guint percentage)
{
	FwupdDevicePrivate *priv = GET_PRIVATE(self);
	g_return_if_fail(FWUPD_IS_DEVICE(self));
	if (priv->percentage == percentage)
		return;
	priv->percentage = percentage;
	g_object_notify(G_OBJECT(self), "percentage");
}

static void
fwupd_device_string_append_update_state(GString *str,
					guint idt,
					const gchar *key,
					FwupdUpdateState value)
{
	if (value == FWUPD_UPDATE_STATE_UNKNOWN)
		return;
	fwupd_codec_string_append(str, idt, key, fwupd_update_state_to_string(value));
}

static void
fwupd_device_add_json(FwupdCodec *codec, JsonBuilder *builder, FwupdCodecFlags flags)
{
	FwupdDevice *self = FWUPD_DEVICE(codec);
	FwupdDevicePrivate *priv = GET_PRIVATE(self);

	fwupd_codec_json_append(builder, FWUPD_RESULT_KEY_NAME, priv->name);
	fwupd_codec_json_append(builder, FWUPD_RESULT_KEY_DEVICE_ID, priv->id);
	fwupd_codec_json_append(builder, FWUPD_RESULT_KEY_PARENT_DEVICE_ID, priv->parent_id);
	fwupd_codec_json_append(builder, FWUPD_RESULT_KEY_COMPOSITE_ID, priv->composite_id);
	if ((flags & FWUPD_CODEC_FLAG_TRUSTED) > 0 && priv->instance_ids != NULL &&
	    priv->instance_ids->len > 0) {
		json_builder_set_member_name(builder, FWUPD_RESULT_KEY_INSTANCE_IDS);
		json_builder_begin_array(builder);
		for (guint i = 0; i < priv->instance_ids->len; i++) {
			const gchar *instance_id = g_ptr_array_index(priv->instance_ids, i);
			json_builder_add_string_value(builder, instance_id);
		}
		json_builder_end_array(builder);
	}
	if (priv->guids != NULL && priv->guids->len > 0) {
		json_builder_set_member_name(builder, FWUPD_RESULT_KEY_GUID);
		json_builder_begin_array(builder);
		for (guint i = 0; i < priv->guids->len; i++) {
			const gchar *guid = g_ptr_array_index(priv->guids, i);
			json_builder_add_string_value(builder, guid);
		}
		json_builder_end_array(builder);
	}
	if (flags & FWUPD_CODEC_FLAG_TRUSTED)
		fwupd_codec_json_append(builder, FWUPD_RESULT_KEY_SERIAL, priv->serial);
	fwupd_codec_json_append(builder, FWUPD_RESULT_KEY_SUMMARY, priv->summary);
	fwupd_codec_json_append(builder, FWUPD_RESULT_KEY_BRANCH, priv->branch);
	fwupd_codec_json_append(builder, FWUPD_RESULT_KEY_PLUGIN, priv->plugin);
	if (priv->protocols != NULL && priv->protocols->len > 0) {
		json_builder_set_member_name(builder, "Protocols");
		json_builder_begin_array(builder);
		for (guint i = 0; i < priv->protocols->len; i++) {
			const gchar *tmp = g_ptr_array_index(priv->protocols, i);
			json_builder_add_string_value(builder, tmp);
		}
		json_builder_end_array(builder);
	}
	if (priv->issues != NULL && priv->issues->len > 0) {
		json_builder_set_member_name(builder, FWUPD_RESULT_KEY_ISSUES);
		json_builder_begin_array(builder);
		for (guint i = 0; i < priv->issues->len; i++) {
			const gchar *tmp = g_ptr_array_index(priv->issues, i);
			json_builder_add_string_value(builder, tmp);
		}
		json_builder_end_array(builder);
	}
	if (priv->flags != FWUPD_DEVICE_FLAG_NONE) {
		json_builder_set_member_name(builder, FWUPD_RESULT_KEY_FLAGS);
		json_builder_begin_array(builder);
		for (guint i = 0; i < 64; i++) {
			const gchar *tmp;
			if ((priv->flags & ((guint64)1 << i)) == 0)
				continue;
			tmp = fwupd_device_flag_to_string((guint64)1 << i);
			json_builder_add_string_value(builder, tmp);
		}
		json_builder_end_array(builder);
	}
	if (priv->request_flags != FWUPD_REQUEST_FLAG_NONE) {
		json_builder_set_member_name(builder, FWUPD_RESULT_KEY_REQUEST_FLAGS);
		json_builder_begin_array(builder);
		for (guint i = 0; i < 64; i++) {
			const gchar *tmp;
			if ((priv->request_flags & ((guint64)1 << i)) == 0)
				continue;
			tmp = fwupd_request_flag_to_string((guint64)1 << i);
			json_builder_add_string_value(builder, tmp);
		}
		json_builder_end_array(builder);
	}
	if (priv->problems != FWUPD_DEVICE_PROBLEM_NONE) {
		json_builder_set_member_name(builder, FWUPD_RESULT_KEY_PROBLEMS);
		json_builder_begin_array(builder);
		for (guint i = 0; i < 64; i++) {
			const gchar *tmp;
			if ((priv->problems & ((guint64)1 << i)) == 0)
				continue;
			tmp = fwupd_device_problem_to_string((guint64)1 << i);
			json_builder_add_string_value(builder, tmp);
		}
		json_builder_end_array(builder);
	}
	if (priv->checksums != NULL && priv->checksums->len > 0) {
		json_builder_set_member_name(builder, "Checksums");
		json_builder_begin_array(builder);
		for (guint i = 0; i < priv->checksums->len; i++) {
			const gchar *checksum = g_ptr_array_index(priv->checksums, i);
			json_builder_add_string_value(builder, checksum);
		}
		json_builder_end_array(builder);
	}
	fwupd_codec_json_append(builder, FWUPD_RESULT_KEY_VENDOR, priv->vendor);
	if (priv->vendor_ids != NULL && priv->vendor_ids->len > 0) {
		json_builder_set_member_name(builder, "VendorIds");
		json_builder_begin_array(builder);
		for (guint i = 0; i < priv->vendor_ids->len; i++) {
			const gchar *tmp = g_ptr_array_index(priv->vendor_ids, i);
			json_builder_add_string_value(builder, tmp);
		}
		json_builder_end_array(builder);
	}
	fwupd_codec_json_append(builder, FWUPD_RESULT_KEY_VERSION, priv->version);
	fwupd_codec_json_append(builder, FWUPD_RESULT_KEY_VERSION_LOWEST, priv->version_lowest);
	fwupd_codec_json_append(builder,
				FWUPD_RESULT_KEY_VERSION_BOOTLOADER,
				priv->version_bootloader);
	fwupd_codec_json_append(builder,
				FWUPD_RESULT_KEY_VERSION_FORMAT,
				fwupd_version_format_to_string(priv->version_format));
	if (priv->flashes_left > 0) {
		fwupd_codec_json_append_int(builder,
					    FWUPD_RESULT_KEY_FLASHES_LEFT,
					    priv->flashes_left);
	}
	if (priv->battery_level != FWUPD_BATTERY_LEVEL_INVALID) {
		fwupd_codec_json_append_int(builder,
					    FWUPD_RESULT_KEY_BATTERY_LEVEL,
					    priv->battery_level);
	}
	if (priv->battery_threshold != FWUPD_BATTERY_LEVEL_INVALID) {
		fwupd_codec_json_append_int(builder,
					    FWUPD_RESULT_KEY_BATTERY_THRESHOLD,
					    priv->battery_threshold);
	}
	if (priv->version_raw > 0) {
		fwupd_codec_json_append_int(builder,
					    FWUPD_RESULT_KEY_VERSION_RAW,
					    priv->version_raw);
	}
	if (priv->version_lowest_raw > 0)
		fwupd_codec_json_append_int(builder,
					    FWUPD_RESULT_KEY_VERSION_LOWEST_RAW,
					    priv->version_lowest_raw);
	if (priv->version_bootloader_raw > 0)
		fwupd_codec_json_append_int(builder,
					    FWUPD_RESULT_KEY_VERSION_BOOTLOADER_RAW,
					    priv->version_bootloader_raw);
	if (priv->version_build_date > 0) {
		fwupd_codec_json_append_int(builder,
					    FWUPD_RESULT_KEY_VERSION_BUILD_DATE,
					    priv->version_build_date);
	}
	if (priv->icons != NULL && priv->icons->len > 0) {
		json_builder_set_member_name(builder, "Icons");
		json_builder_begin_array(builder);
		for (guint i = 0; i < priv->icons->len; i++) {
			const gchar *icon = g_ptr_array_index(priv->icons, i);
			json_builder_add_string_value(builder, icon);
		}
		json_builder_end_array(builder);
	}
	if (priv->install_duration > 0) {
		fwupd_codec_json_append_int(builder,
					    FWUPD_RESULT_KEY_INSTALL_DURATION,
					    priv->install_duration);
	}
	if (priv->created > 0)
		fwupd_codec_json_append_int(builder, FWUPD_RESULT_KEY_CREATED, priv->created);
	if (priv->modified > 0)
		fwupd_codec_json_append_int(builder, FWUPD_RESULT_KEY_MODIFIED, priv->modified);
	if (priv->update_state > 0) {
		fwupd_codec_json_append_int(builder,
					    FWUPD_RESULT_KEY_UPDATE_STATE,
					    priv->update_state);
	}
	if (priv->status > 0)
		fwupd_codec_json_append_int(builder, FWUPD_RESULT_KEY_STATUS, priv->status);
	if (priv->percentage > 0)
		fwupd_codec_json_append_int(builder, FWUPD_RESULT_KEY_PERCENTAGE, priv->percentage);
	fwupd_codec_json_append(builder, FWUPD_RESULT_KEY_UPDATE_ERROR, priv->update_error);
	if (priv->releases != NULL && priv->releases->len > 0)
		fwupd_codec_array_to_json(priv->releases, "Releases", builder, flags);
}

static gboolean
fwupd_device_from_json(FwupdCodec *codec, JsonNode *json_node, GError **error)
{
	FwupdDevice *self = FWUPD_DEVICE(codec);
	JsonObject *obj;

	g_return_val_if_fail(FWUPD_IS_DEVICE(self), FALSE);
	g_return_val_if_fail(json_node != NULL, FALSE);
	g_return_val_if_fail(error == NULL || *error == NULL, FALSE);

	/* sanity check */
	if (!JSON_NODE_HOLDS_OBJECT(json_node)) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INVALID_DATA,
				    "not JSON object");
		return FALSE;
	}
	obj = json_node_get_object(json_node);

	/* this has to exist */
	if (!json_object_has_member(obj, FWUPD_RESULT_KEY_DEVICE_ID)) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INVALID_DATA,
			    "no %s property in object",
			    FWUPD_RESULT_KEY_DEVICE_ID);
		return FALSE;
	}
	fwupd_device_set_id(self, json_object_get_string_member(obj, FWUPD_RESULT_KEY_DEVICE_ID));

	/* also optional */
	if (json_object_has_member(obj, FWUPD_RESULT_KEY_NAME)) {
		const gchar *tmp =
		    json_object_get_string_member_with_default(obj, FWUPD_RESULT_KEY_NAME, NULL);
		fwupd_device_set_name(self, tmp);
	}
	if (json_object_has_member(obj, FWUPD_RESULT_KEY_PARENT_DEVICE_ID)) {
		const gchar *tmp =
		    json_object_get_string_member_with_default(obj,
							       FWUPD_RESULT_KEY_PARENT_DEVICE_ID,
							       NULL);
		fwupd_device_set_parent_id(self, tmp);
	}
	if (json_object_has_member(obj, FWUPD_RESULT_KEY_COMPOSITE_ID)) {
		const gchar *tmp =
		    json_object_get_string_member_with_default(obj,
							       FWUPD_RESULT_KEY_COMPOSITE_ID,
							       NULL);
		fwupd_device_set_composite_id(self, tmp);
	}
	if (json_object_has_member(obj, FWUPD_RESULT_KEY_PROTOCOL)) {
		const gchar *tmp =
		    json_object_get_string_member_with_default(obj,
							       FWUPD_RESULT_KEY_PROTOCOL,
							       NULL);
		fwupd_device_add_protocol(self, tmp);
	}
	if (json_object_has_member(obj, FWUPD_RESULT_KEY_SERIAL)) {
		const gchar *tmp =
		    json_object_get_string_member_with_default(obj, FWUPD_RESULT_KEY_SERIAL, NULL);
		fwupd_device_set_serial(self, tmp);
	}
	if (json_object_has_member(obj, FWUPD_RESULT_KEY_SUMMARY)) {
		const gchar *tmp =
		    json_object_get_string_member_with_default(obj, FWUPD_RESULT_KEY_SUMMARY, NULL);
		fwupd_device_set_summary(self, tmp);
	}
	if (json_object_has_member(obj, FWUPD_RESULT_KEY_BRANCH)) {
		const gchar *tmp =
		    json_object_get_string_member_with_default(obj, FWUPD_RESULT_KEY_BRANCH, NULL);
		fwupd_device_set_branch(self, tmp);
	}
	if (json_object_has_member(obj, FWUPD_RESULT_KEY_PLUGIN)) {
		const gchar *tmp =
		    json_object_get_string_member_with_default(obj, FWUPD_RESULT_KEY_PLUGIN, NULL);
		fwupd_device_set_plugin(self, tmp);
	}
	if (json_object_has_member(obj, FWUPD_RESULT_KEY_VENDOR)) {
		const gchar *tmp =
		    json_object_get_string_member_with_default(obj, FWUPD_RESULT_KEY_VENDOR, NULL);
		fwupd_device_set_vendor(self, tmp);
	}
	if (json_object_has_member(obj, FWUPD_RESULT_KEY_VENDOR_ID)) {
		const gchar *tmp =
		    json_object_get_string_member_with_default(obj,
							       FWUPD_RESULT_KEY_VENDOR_ID,
							       NULL);
		if (tmp != NULL) {
			g_auto(GStrv) split = g_strsplit(tmp, "|", -1);
			for (guint i = 0; split[i] != NULL; i++)
				fwupd_device_add_vendor_id(self, split[i]);
		}
	}
	if (json_object_has_member(obj, FWUPD_RESULT_KEY_VERSION)) {
		const gchar *tmp =
		    json_object_get_string_member_with_default(obj, FWUPD_RESULT_KEY_VERSION, NULL);
		fwupd_device_set_version(self, tmp);
	}
	if (json_object_has_member(obj, FWUPD_RESULT_KEY_VERSION_LOWEST)) {
		const gchar *tmp =
		    json_object_get_string_member_with_default(obj,
							       FWUPD_RESULT_KEY_VERSION_LOWEST,
							       NULL);
		fwupd_device_set_version_lowest(self, tmp);
	}
	if (json_object_has_member(obj, FWUPD_RESULT_KEY_VERSION_BOOTLOADER)) {
		const gchar *tmp =
		    json_object_get_string_member_with_default(obj,
							       FWUPD_RESULT_KEY_VERSION_BOOTLOADER,
							       NULL);
		fwupd_device_set_version_bootloader(self, tmp);
	}
	if (json_object_has_member(obj, FWUPD_RESULT_KEY_VERSION_FORMAT)) {
		const gchar *tmp =
		    json_object_get_string_member_with_default(obj,
							       FWUPD_RESULT_KEY_VERSION_FORMAT,
							       NULL);
		fwupd_device_set_version_format(self, fwupd_version_format_from_string(tmp));
	}
	if (json_object_has_member(obj, FWUPD_RESULT_KEY_FLASHES_LEFT)) {
		gint64 tmp =
		    json_object_get_int_member_with_default(obj, FWUPD_RESULT_KEY_FLASHES_LEFT, 0);
		fwupd_device_set_flashes_left(self, tmp);
	}
	if (json_object_has_member(obj, FWUPD_RESULT_KEY_BATTERY_LEVEL)) {
		gint64 tmp =
		    json_object_get_int_member_with_default(obj, FWUPD_RESULT_KEY_BATTERY_LEVEL, 0);
		fwupd_device_set_battery_level(self, tmp);
	}
	if (json_object_has_member(obj, FWUPD_RESULT_KEY_BATTERY_THRESHOLD)) {
		gint64 tmp =
		    json_object_get_int_member_with_default(obj,
							    FWUPD_RESULT_KEY_BATTERY_THRESHOLD,
							    0);
		fwupd_device_set_battery_threshold(self, tmp);
	}
	if (json_object_has_member(obj, FWUPD_RESULT_KEY_VERSION_RAW)) {
		gint64 tmp =
		    json_object_get_int_member_with_default(obj, FWUPD_RESULT_KEY_VERSION_RAW, 0);
		fwupd_device_set_version_raw(self, tmp);
	}
	if (json_object_has_member(obj, FWUPD_RESULT_KEY_VERSION_LOWEST_RAW)) {
		gint64 tmp =
		    json_object_get_int_member_with_default(obj,
							    FWUPD_RESULT_KEY_VERSION_LOWEST_RAW,
							    0);
		fwupd_device_set_version_lowest_raw(self, tmp);
	}
	if (json_object_has_member(obj, FWUPD_RESULT_KEY_VERSION_BOOTLOADER_RAW)) {
		gint64 tmp =
		    json_object_get_int_member_with_default(obj,
							    FWUPD_RESULT_KEY_VERSION_BOOTLOADER_RAW,
							    0);
		fwupd_device_set_version_bootloader_raw(self, tmp);
	}
	if (json_object_has_member(obj, FWUPD_RESULT_KEY_VERSION_BUILD_DATE)) {
		gint64 tmp =
		    json_object_get_int_member_with_default(obj,
							    FWUPD_RESULT_KEY_VERSION_BUILD_DATE,
							    0);
		fwupd_device_set_version_build_date(self, tmp);
	}
	if (json_object_has_member(obj, FWUPD_RESULT_KEY_INSTALL_DURATION)) {
		gint64 tmp =
		    json_object_get_int_member_with_default(obj,
							    FWUPD_RESULT_KEY_INSTALL_DURATION,
							    0);
		fwupd_device_set_install_duration(self, tmp);
	}
	if (json_object_has_member(obj, FWUPD_RESULT_KEY_CREATED)) {
		gint64 tmp =
		    json_object_get_int_member_with_default(obj, FWUPD_RESULT_KEY_CREATED, 0);
		fwupd_device_set_created(self, tmp);
	}
	if (json_object_has_member(obj, FWUPD_RESULT_KEY_MODIFIED)) {
		gint64 tmp =
		    json_object_get_int_member_with_default(obj, FWUPD_RESULT_KEY_MODIFIED, 0);
		fwupd_device_set_modified(self, tmp);
	}
	if (json_object_has_member(obj, FWUPD_RESULT_KEY_UPDATE_STATE)) {
		const gchar *tmp =
		    json_object_get_string_member_with_default(obj,
							       FWUPD_RESULT_KEY_UPDATE_STATE,
							       NULL);
		fwupd_device_set_update_state(self, fwupd_update_state_from_string(tmp));
	}
	if (json_object_has_member(obj, FWUPD_RESULT_KEY_STATUS)) {
		const gchar *tmp =
		    json_object_get_string_member_with_default(obj, FWUPD_RESULT_KEY_STATUS, NULL);
		fwupd_device_set_status(self, fwupd_status_from_string(tmp));
	}
	if (json_object_has_member(obj, FWUPD_RESULT_KEY_PERCENTAGE)) {
		gint64 tmp =
		    json_object_get_int_member_with_default(obj, FWUPD_RESULT_KEY_PERCENTAGE, 0);
		fwupd_device_set_percentage(self, tmp);
	}
	if (json_object_has_member(obj, FWUPD_RESULT_KEY_UPDATE_ERROR)) {
		const gchar *tmp =
		    json_object_get_string_member_with_default(obj,
							       FWUPD_RESULT_KEY_UPDATE_ERROR,
							       NULL);
		fwupd_device_set_update_error(self, tmp);
	}
	if (json_object_has_member(obj, FWUPD_RESULT_KEY_INSTANCE_IDS)) {
		JsonArray *array = json_object_get_array_member(obj, FWUPD_RESULT_KEY_INSTANCE_IDS);
		for (guint i = 0; i < json_array_get_length(array); i++)
			fwupd_device_add_instance_id(self, json_array_get_string_element(array, i));
	}
	if (json_object_has_member(obj, FWUPD_RESULT_KEY_GUID)) {
		JsonArray *array = json_object_get_array_member(obj, FWUPD_RESULT_KEY_GUID);
		for (guint i = 0; i < json_array_get_length(array); i++)
			fwupd_device_add_guid(self, json_array_get_string_element(array, i));
	}
	if (json_object_has_member(obj, FWUPD_RESULT_KEY_ISSUES)) {
		JsonArray *array = json_object_get_array_member(obj, FWUPD_RESULT_KEY_ISSUES);
		for (guint i = 0; i < json_array_get_length(array); i++)
			fwupd_device_add_issue(self, json_array_get_string_element(array, i));
	}
	if (json_object_has_member(obj, FWUPD_RESULT_KEY_FLAGS)) {
		JsonArray *array = json_object_get_array_member(obj, FWUPD_RESULT_KEY_FLAGS);
		for (guint i = 0; i < json_array_get_length(array); i++) {
			const gchar *tmp = json_array_get_string_element(array, i);
			fwupd_device_add_flag(self, fwupd_device_flag_from_string(tmp));
		}
	}
	if (json_object_has_member(obj, FWUPD_RESULT_KEY_PROBLEMS)) {
		JsonArray *array = json_object_get_array_member(obj, FWUPD_RESULT_KEY_PROBLEMS);
		for (guint i = 0; i < json_array_get_length(array); i++) {
			const gchar *tmp = json_array_get_string_element(array, i);
			fwupd_device_add_problem(self, fwupd_device_problem_from_string(tmp));
		}
	}
	if (json_object_has_member(obj, FWUPD_RESULT_KEY_REQUEST_FLAGS)) {
		JsonArray *array =
		    json_object_get_array_member(obj, FWUPD_RESULT_KEY_REQUEST_FLAGS);
		for (guint i = 0; i < json_array_get_length(array); i++) {
			const gchar *tmp = json_array_get_string_element(array, i);
			fwupd_device_add_request_flag(self, fwupd_request_flag_from_string(tmp));
		}
	}
	if (json_object_has_member(obj, "VendorIds")) {
		JsonArray *array = json_object_get_array_member(obj, "VendorIds");
		for (guint i = 0; i < json_array_get_length(array); i++)
			fwupd_device_add_vendor_id(self, json_array_get_string_element(array, i));
	}
	if (json_object_has_member(obj, "Protocols")) {
		JsonArray *array = json_object_get_array_member(obj, "Protocols");
		for (guint i = 0; i < json_array_get_length(array); i++)
			fwupd_device_add_protocol(self, json_array_get_string_element(array, i));
	}
	if (json_object_has_member(obj, "Icons")) {
		JsonArray *array = json_object_get_array_member(obj, "Icons");
		for (guint i = 0; i < json_array_get_length(array); i++)
			fwupd_device_add_icon(self, json_array_get_string_element(array, i));
	}
	if (json_object_has_member(obj, "Checksums")) {
		JsonArray *array = json_object_get_array_member(obj, "Checksums");
		for (guint i = 0; i < json_array_get_length(array); i++)
			fwupd_device_add_checksum(self, json_array_get_string_element(array, i));
	}

	/* success */
	return TRUE;
}

static gchar *
fwupd_device_verstr_raw(guint64 value_raw)
{
	if (value_raw > 0xffffffff) {
		return g_strdup_printf("0x%08x%08x",
				       (guint)(value_raw >> 32),
				       (guint)(value_raw & 0xffffffff));
	}
	return g_strdup_printf("0x%08x", (guint)value_raw);
}

typedef struct {
	gchar *guid;
	gchar *instance_id;
} FwupdDeviceGuidHelper;

static void
fwupd_device_guid_helper_new(FwupdDeviceGuidHelper *helper)
{
	g_free(helper->guid);
	g_free(helper->instance_id);
	g_free(helper);
}

static FwupdDeviceGuidHelper *
fwupd_device_guid_helper_array_find(GPtrArray *array, const gchar *guid)
{
	for (guint i = 0; i < array->len; i++) {
		FwupdDeviceGuidHelper *helper = g_ptr_array_index(array, i);
		if (g_strcmp0(helper->guid, guid) == 0)
			return helper;
	}
	return NULL;
}

static void
fwupd_device_add_string(FwupdCodec *codec, guint idt, GString *str)
{
	FwupdDevice *self = FWUPD_DEVICE(codec);
	FwupdDevicePrivate *priv = GET_PRIVATE(self);
	g_autoptr(GPtrArray) guid_helpers = NULL;

	fwupd_codec_string_append(str, idt, FWUPD_RESULT_KEY_DEVICE_ID, priv->id);
	if (g_strcmp0(priv->composite_id, priv->parent_id) != 0) {
		fwupd_codec_string_append(str,
					  idt,
					  FWUPD_RESULT_KEY_PARENT_DEVICE_ID,
					  priv->parent_id);
	}
	fwupd_codec_string_append(str, idt, FWUPD_RESULT_KEY_COMPOSITE_ID, priv->composite_id);
	fwupd_codec_string_append(str, idt, FWUPD_RESULT_KEY_NAME, priv->name);
	if (priv->status != FWUPD_STATUS_UNKNOWN) {
		fwupd_codec_string_append(str,
					  idt,
					  FWUPD_RESULT_KEY_STATUS,
					  fwupd_status_to_string(priv->status));
	}
	fwupd_codec_string_append_int(str, idt, FWUPD_RESULT_KEY_PERCENTAGE, priv->percentage);

	/* show instance IDs optionally mapped to GUIDs, and also "standalone" GUIDs */
	guid_helpers = g_ptr_array_new_with_free_func((GDestroyNotify)fwupd_device_guid_helper_new);
	if (priv->instance_ids != NULL) {
		for (guint i = 0; i < priv->instance_ids->len; i++) {
			FwupdDeviceGuidHelper *helper = g_new0(FwupdDeviceGuidHelper, 1);
			const gchar *instance_id = g_ptr_array_index(priv->instance_ids, i);
			helper->guid = fwupd_guid_hash_string(instance_id);
			helper->instance_id = g_strdup(instance_id);
			g_ptr_array_add(guid_helpers, helper);
		}
	}
	if (priv->guids != NULL) {
		for (guint i = 0; i < priv->guids->len; i++) {
			const gchar *guid = g_ptr_array_index(priv->guids, i);
			if (fwupd_device_guid_helper_array_find(guid_helpers, guid) == NULL) {
				FwupdDeviceGuidHelper *helper = g_new0(FwupdDeviceGuidHelper, 1);
				helper->guid = g_strdup(guid);
				g_ptr_array_add(guid_helpers, helper);
			}
		}
	}
	for (guint i = 0; i < guid_helpers->len; i++) {
		FwupdDeviceGuidHelper *helper = g_ptr_array_index(guid_helpers, i);
		g_autoptr(GString) tmp = g_string_new(helper->guid);
		if (helper->instance_id != NULL)
			g_string_append_printf(tmp, " ← %s", helper->instance_id);
		if (!fwupd_device_has_guid(self, helper->guid))
			g_string_append(tmp, " ⚠");
		fwupd_codec_string_append(str, idt, FWUPD_RESULT_KEY_GUID, tmp->str);
	}

	fwupd_codec_string_append(str, idt, FWUPD_RESULT_KEY_SERIAL, priv->serial);
	fwupd_codec_string_append(str, idt, FWUPD_RESULT_KEY_SUMMARY, priv->summary);
	fwupd_codec_string_append(str, idt, FWUPD_RESULT_KEY_BRANCH, priv->branch);
	fwupd_codec_string_append(str, idt, FWUPD_RESULT_KEY_PLUGIN, priv->plugin);
	if (priv->protocols != NULL) {
		for (guint i = 0; i < priv->protocols->len; i++) {
			const gchar *tmp = g_ptr_array_index(priv->protocols, i);
			fwupd_codec_string_append(str, idt, FWUPD_RESULT_KEY_PROTOCOL, tmp);
		}
	}
	if (priv->issues != NULL) {
		for (guint i = 0; i < priv->issues->len; i++) {
			const gchar *tmp = g_ptr_array_index(priv->issues, i);
			fwupd_codec_string_append(str, idt, FWUPD_RESULT_KEY_ISSUES, tmp);
		}
	}
	fwupd_device_string_append_flags(str, idt, FWUPD_RESULT_KEY_FLAGS, priv->flags);
	if (priv->problems != FWUPD_DEVICE_PROBLEM_NONE) {
		fwupd_device_string_append_problems(str,
						    idt,
						    FWUPD_RESULT_KEY_PROBLEMS,
						    priv->problems);
	}
	if (priv->request_flags > 0) {
		fwupd_device_string_append_request_flags(str,
							 idt,
							 FWUPD_RESULT_KEY_REQUEST_FLAGS,
							 priv->request_flags);
	}
	if (priv->checksums != NULL) {
		for (guint i = 0; i < priv->checksums->len; i++) {
			const gchar *checksum = g_ptr_array_index(priv->checksums, i);
			g_autofree gchar *checksum_display =
			    fwupd_checksum_format_for_display(checksum);
			fwupd_codec_string_append(str,
						  idt,
						  FWUPD_RESULT_KEY_CHECKSUM,
						  checksum_display);
		}
	}
	fwupd_codec_string_append(str, idt, FWUPD_RESULT_KEY_VENDOR, priv->vendor);
	if (priv->vendor_ids != NULL) {
		for (guint i = 0; i < priv->vendor_ids->len; i++) {
			const gchar *tmp = g_ptr_array_index(priv->vendor_ids, i);
			fwupd_codec_string_append(str, idt, FWUPD_RESULT_KEY_VENDOR_ID, tmp);
		}
	}
	fwupd_codec_string_append(str, idt, FWUPD_RESULT_KEY_VERSION, priv->version);
	fwupd_codec_string_append(str, idt, FWUPD_RESULT_KEY_VERSION_LOWEST, priv->version_lowest);
	fwupd_codec_string_append(str,
				  idt,
				  FWUPD_RESULT_KEY_VERSION_BOOTLOADER,
				  priv->version_bootloader);
	fwupd_codec_string_append(str,
				  idt,
				  FWUPD_RESULT_KEY_VERSION_FORMAT,
				  fwupd_version_format_to_string(priv->version_format));
	if (priv->flashes_left < 2) {
		fwupd_codec_string_append_int(str,
					      idt,
					      FWUPD_RESULT_KEY_FLASHES_LEFT,
					      priv->flashes_left);
	}
	if (priv->battery_level != FWUPD_BATTERY_LEVEL_INVALID) {
		fwupd_codec_string_append_int(str,
					      idt,
					      FWUPD_RESULT_KEY_BATTERY_LEVEL,
					      priv->battery_level);
	}
	if (priv->battery_threshold != FWUPD_BATTERY_LEVEL_INVALID) {
		fwupd_codec_string_append_int(str,
					      idt,
					      FWUPD_RESULT_KEY_BATTERY_THRESHOLD,
					      priv->battery_threshold);
	}
	if (priv->version_raw > 0) {
		g_autofree gchar *tmp = fwupd_device_verstr_raw(priv->version_raw);
		fwupd_codec_string_append(str, idt, FWUPD_RESULT_KEY_VERSION_RAW, tmp);
	}
	if (priv->version_lowest_raw > 0) {
		g_autofree gchar *tmp = fwupd_device_verstr_raw(priv->version_lowest_raw);
		fwupd_codec_string_append(str, idt, FWUPD_RESULT_KEY_VERSION_LOWEST_RAW, tmp);
	}
	fwupd_codec_string_append_time(str,
				       idt,
				       FWUPD_RESULT_KEY_VERSION_BUILD_DATE,
				       priv->version_build_date);
	if (priv->version_bootloader_raw > 0) {
		g_autofree gchar *tmp = fwupd_device_verstr_raw(priv->version_bootloader_raw);
		fwupd_codec_string_append(str, idt, FWUPD_RESULT_KEY_VERSION_BOOTLOADER_RAW, tmp);
	}
	if (priv->icons != NULL && priv->icons->len > 0) {
		g_autoptr(GString) tmp = g_string_new(NULL);
		for (guint i = 0; i < priv->icons->len; i++) {
			const gchar *icon = g_ptr_array_index(priv->icons, i);
			g_string_append_printf(tmp, "%s,", icon);
		}
		if (tmp->len > 1)
			g_string_truncate(tmp, tmp->len - 1);
		fwupd_codec_string_append(str, idt, FWUPD_RESULT_KEY_ICON, tmp->str);
	}
	fwupd_codec_string_append_int(str,
				      idt,
				      FWUPD_RESULT_KEY_INSTALL_DURATION,
				      priv->install_duration);
	fwupd_codec_string_append_time(str, idt, FWUPD_RESULT_KEY_CREATED, priv->created);
	fwupd_codec_string_append_time(str, idt, FWUPD_RESULT_KEY_MODIFIED, priv->modified);
	fwupd_device_string_append_update_state(str,
						idt,
						FWUPD_RESULT_KEY_UPDATE_STATE,
						priv->update_state);
	fwupd_codec_string_append(str, idt, FWUPD_RESULT_KEY_UPDATE_ERROR, priv->update_error);
	if (priv->releases != NULL) {
		for (guint i = 0; i < priv->releases->len; i++) {
			FwupdRelease *release = g_ptr_array_index(priv->releases, i);
			fwupd_codec_add_string(FWUPD_CODEC(release), idt, str);
		}
	}
}

static void
fwupd_device_get_property(GObject *object, guint prop_id, GValue *value, GParamSpec *pspec)
{
	FwupdDevice *self = FWUPD_DEVICE(object);
	FwupdDevicePrivate *priv = GET_PRIVATE(self);
	switch (prop_id) {
	case PROP_ID:
		g_value_set_string(value, priv->id);
		break;
	case PROP_VERSION:
		g_value_set_string(value, priv->version);
		break;
	case PROP_VERSION_FORMAT:
		g_value_set_uint(value, priv->version_format);
		break;
	case PROP_FLAGS:
		g_value_set_uint64(value, priv->flags);
		break;
	case PROP_PROBLEMS:
		g_value_set_uint64(value, priv->problems);
		break;
	case PROP_REQUEST_FLAGS:
		g_value_set_uint64(value, priv->request_flags);
		break;
	case PROP_UPDATE_ERROR:
		g_value_set_string(value, priv->update_error);
		break;
	case PROP_STATUS:
		g_value_set_uint(value, priv->status);
		break;
	case PROP_PERCENTAGE:
		g_value_set_uint(value, priv->percentage);
		break;
	case PROP_PARENT:
		g_value_set_object(value, priv->parent);
		break;
	case PROP_UPDATE_STATE:
		g_value_set_uint(value, priv->update_state);
		break;
	case PROP_BATTERY_LEVEL:
		g_value_set_uint(value, priv->battery_level);
		break;
	case PROP_BATTERY_THRESHOLD:
		g_value_set_uint(value, priv->battery_threshold);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
		break;
	}
}

static void
fwupd_device_set_property(GObject *object, guint prop_id, const GValue *value, GParamSpec *pspec)
{
	FwupdDevice *self = FWUPD_DEVICE(object);
	switch (prop_id) {
	case PROP_VERSION:
		fwupd_device_set_version(self, g_value_get_string(value));
		break;
	case PROP_ID:
		fwupd_device_set_id(self, g_value_get_string(value));
		break;
	case PROP_VERSION_FORMAT:
		fwupd_device_set_version_format(self, g_value_get_uint(value));
		break;
	case PROP_FLAGS:
		fwupd_device_set_flags(self, g_value_get_uint64(value));
		break;
	case PROP_PROBLEMS:
		fwupd_device_set_problems(self, g_value_get_uint64(value));
		break;
	case PROP_REQUEST_FLAGS:
		fwupd_device_set_request_flags(self, g_value_get_uint64(value));
		break;
	case PROP_UPDATE_ERROR:
		fwupd_device_set_update_error(self, g_value_get_string(value));
		break;
	case PROP_STATUS:
		fwupd_device_set_status(self, g_value_get_uint(value));
		break;
	case PROP_PERCENTAGE:
		fwupd_device_set_percentage(self, g_value_get_uint(value));
		break;
	case PROP_PARENT:
		fwupd_device_set_parent(self, g_value_get_object(value));
		break;
	case PROP_UPDATE_STATE:
		fwupd_device_set_update_state(self, g_value_get_uint(value));
		break;
	case PROP_BATTERY_LEVEL:
		fwupd_device_set_battery_level(self, g_value_get_uint(value));
		break;
	case PROP_BATTERY_THRESHOLD:
		fwupd_device_set_battery_threshold(self, g_value_get_uint(value));
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
		break;
	}
}

static void
fwupd_device_class_init(FwupdDeviceClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS(klass);
	GParamSpec *pspec;

	object_class->finalize = fwupd_device_finalize;
	object_class->get_property = fwupd_device_get_property;
	object_class->set_property = fwupd_device_set_property;

	/**
	 * FwupdDevice:version:
	 *
	 * The device version.
	 *
	 * Since: 1.8.15
	 */
	pspec = g_param_spec_string("version",
				    NULL,
				    NULL,
				    NULL,
				    G_PARAM_READWRITE | G_PARAM_STATIC_NAME);
	g_object_class_install_property(object_class, PROP_VERSION, pspec);

	/**
	 * FwupdDevice:id:
	 *
	 * The device ID.
	 *
	 * Since: 2.0.0
	 */
	pspec =
	    g_param_spec_string("id", NULL, NULL, NULL, G_PARAM_READWRITE | G_PARAM_STATIC_NAME);
	g_object_class_install_property(object_class, PROP_ID, pspec);

	/**
	 * FwupdDevice:version-format:
	 *
	 * The version format of the device.
	 *
	 * Since: 1.2.9
	 */
	pspec = g_param_spec_uint("version-format",
				  NULL,
				  NULL,
				  FWUPD_VERSION_FORMAT_UNKNOWN,
				  FWUPD_VERSION_FORMAT_LAST,
				  FWUPD_VERSION_FORMAT_UNKNOWN,
				  G_PARAM_READWRITE | G_PARAM_STATIC_NAME);
	g_object_class_install_property(object_class, PROP_VERSION_FORMAT, pspec);

	/**
	 * FwupdDevice:flags:
	 *
	 * The device flags.
	 *
	 * Since: 0.9.3
	 */
	pspec = g_param_spec_uint64("flags",
				    NULL,
				    NULL,
				    FWUPD_DEVICE_FLAG_NONE,
				    FWUPD_DEVICE_FLAG_UNKNOWN,
				    FWUPD_DEVICE_FLAG_NONE,
				    G_PARAM_READWRITE | G_PARAM_STATIC_NAME);
	g_object_class_install_property(object_class, PROP_FLAGS, pspec);

	/**
	 * FwupdDevice:problems:
	 *
	 * The problems with the device that the user could fix, e.g. "lid open".
	 *
	 * Since: 1.8.1
	 */
	pspec = g_param_spec_uint64("problems",
				    NULL,
				    NULL,
				    FWUPD_DEVICE_PROBLEM_NONE,
				    FWUPD_DEVICE_PROBLEM_UNKNOWN,
				    FWUPD_DEVICE_PROBLEM_NONE,
				    G_PARAM_READWRITE | G_PARAM_STATIC_NAME);
	g_object_class_install_property(object_class, PROP_PROBLEMS, pspec);

	/**
	 * FwupdDevice:request-flags:
	 *
	 * The device request flags.
	 *
	 * Since: 1.9.10
	 */
	pspec = g_param_spec_uint64("request-flags",
				    NULL,
				    NULL,
				    FWUPD_REQUEST_FLAG_NONE,
				    FWUPD_REQUEST_FLAG_UNKNOWN,
				    FWUPD_REQUEST_FLAG_NONE,
				    G_PARAM_READWRITE | G_PARAM_STATIC_NAME);
	g_object_class_install_property(object_class, PROP_REQUEST_FLAGS, pspec);

	/**
	 * FwupdDevice:status:
	 *
	 * The current device status.
	 *
	 * Since: 1.4.0
	 */
	pspec = g_param_spec_uint("status",
				  NULL,
				  NULL,
				  FWUPD_STATUS_UNKNOWN,
				  FWUPD_STATUS_LAST,
				  FWUPD_STATUS_UNKNOWN,
				  G_PARAM_READWRITE | G_PARAM_STATIC_NAME);
	g_object_class_install_property(object_class, PROP_STATUS, pspec);

	/**
	 * FwupdDevice:percentage:
	 *
	 * The current device percentage.
	 *
	 * Since: 1.8.11
	 */
	pspec = g_param_spec_uint("percentage",
				  NULL,
				  NULL,
				  0,
				  100,
				  0,
				  G_PARAM_READWRITE | G_PARAM_STATIC_NAME);
	g_object_class_install_property(object_class, PROP_PERCENTAGE, pspec);

	/**
	 * FwupdDevice:parent:
	 *
	 * The device parent.
	 *
	 * Since: 1.0.8
	 */
	pspec = g_param_spec_object("parent",
				    NULL,
				    NULL,
				    FWUPD_TYPE_DEVICE,
				    G_PARAM_READWRITE | G_PARAM_CONSTRUCT | G_PARAM_STATIC_NAME);
	g_object_class_install_property(object_class, PROP_PARENT, pspec);

	/**
	 * FwupdDevice:update-state:
	 *
	 * The device update state.
	 *
	 * Since: 0.9.8
	 */
	pspec = g_param_spec_uint("update-state",
				  NULL,
				  NULL,
				  FWUPD_UPDATE_STATE_UNKNOWN,
				  FWUPD_UPDATE_STATE_LAST,
				  FWUPD_UPDATE_STATE_UNKNOWN,
				  G_PARAM_READWRITE | G_PARAM_STATIC_NAME);
	g_object_class_install_property(object_class, PROP_UPDATE_STATE, pspec);

	/**
	 * FwupdDevice:update-error:
	 *
	 * The device update error.
	 *
	 * Since: 0.9.8
	 */
	pspec = g_param_spec_string("update-error",
				    NULL,
				    NULL,
				    NULL,
				    G_PARAM_READWRITE | G_PARAM_STATIC_NAME);
	g_object_class_install_property(object_class, PROP_UPDATE_ERROR, pspec);

	/**
	 * FwupdDevice:battery-level:
	 *
	 * The device battery level in percent.
	 *
	 * Since: 1.5.8
	 */
	pspec = g_param_spec_uint("battery-level",
				  NULL,
				  NULL,
				  0,
				  FWUPD_BATTERY_LEVEL_INVALID,
				  FWUPD_BATTERY_LEVEL_INVALID,
				  G_PARAM_READWRITE | G_PARAM_STATIC_NAME);
	g_object_class_install_property(object_class, PROP_BATTERY_LEVEL, pspec);

	/**
	 * FwupdDevice:battery-threshold:
	 *
	 * The device battery threshold in percent.
	 *
	 * Since: 1.5.8
	 */
	pspec = g_param_spec_uint("battery-threshold",
				  NULL,
				  NULL,
				  0,
				  FWUPD_BATTERY_LEVEL_INVALID,
				  FWUPD_BATTERY_LEVEL_INVALID,
				  G_PARAM_READWRITE | G_PARAM_STATIC_NAME);
	g_object_class_install_property(object_class, PROP_BATTERY_THRESHOLD, pspec);
}

static void
fwupd_device_init(FwupdDevice *self)
{
	FwupdDevicePrivate *priv = GET_PRIVATE(self);
	priv->battery_level = FWUPD_BATTERY_LEVEL_INVALID;
	priv->battery_threshold = FWUPD_BATTERY_LEVEL_INVALID;
}

static void
fwupd_device_finalize(GObject *object)
{
	FwupdDevice *self = FWUPD_DEVICE(object);
	FwupdDevicePrivate *priv = GET_PRIVATE(self);

	if (priv->parent != NULL)
		g_object_remove_weak_pointer(G_OBJECT(priv->parent), (gpointer *)&priv->parent);
	if (priv->children != NULL) {
		for (guint i = 0; i < priv->children->len; i++) {
			FwupdDevice *child = g_ptr_array_index(priv->children, i);
			g_object_weak_unref(G_OBJECT(child), fwupd_device_child_finalized_cb, self);
		}
	}

	g_free(priv->id);
	g_free(priv->parent_id);
	g_free(priv->composite_id);
	g_free(priv->name);
	g_free(priv->serial);
	g_free(priv->summary);
	g_free(priv->branch);
	g_free(priv->vendor);
	g_free(priv->plugin);
	g_free(priv->update_error);
	g_free(priv->version);
	g_free(priv->version_lowest);
	g_free(priv->version_bootloader);
	if (priv->guids != NULL)
		g_ptr_array_unref(priv->guids);
	if (priv->vendor_ids != NULL)
		g_ptr_array_unref(priv->vendor_ids);
	if (priv->protocols != NULL)
		g_ptr_array_unref(priv->protocols);
	if (priv->instance_ids != NULL)
		g_ptr_array_unref(priv->instance_ids);
	if (priv->icons != NULL)
		g_ptr_array_unref(priv->icons);
	if (priv->checksums != NULL)
		g_ptr_array_unref(priv->checksums);
	if (priv->children != NULL)
		g_ptr_array_unref(priv->children);
	if (priv->releases != NULL)
		g_ptr_array_unref(priv->releases);
	if (priv->issues != NULL)
		g_ptr_array_unref(priv->issues);

	G_OBJECT_CLASS(fwupd_device_parent_class)->finalize(object);
}

static void
fwupd_device_from_variant_iter(FwupdCodec *codec, GVariantIter *iter)
{
	FwupdDevice *self = FWUPD_DEVICE(codec);
	GVariant *value;
	const gchar *key;
	while (g_variant_iter_next(iter, "{&sv}", &key, &value)) {
		fwupd_device_from_key_value(self, key, value);
		g_variant_unref(value);
	}
}

static void
fwupd_device_codec_iface_init(FwupdCodecInterface *iface)
{
	iface->add_string = fwupd_device_add_string;
	iface->add_json = fwupd_device_add_json;
	iface->from_json = fwupd_device_from_json;
	iface->add_variant = fwupd_device_add_variant;
	iface->from_variant_iter = fwupd_device_from_variant_iter;
}

/**
 * fwupd_device_array_ensure_parents:
 * @devices: (not nullable) (element-type FwupdDevice): devices
 *
 * Sets the parent object on all devices in the array using the parent ID.
 *
 * Since: 1.3.7
 **/
void
fwupd_device_array_ensure_parents(GPtrArray *devices)
{
	g_autoptr(GHashTable) devices_by_id = NULL;

	g_return_if_fail(devices != NULL);

	/* create hash of ID->FwupdDevice */
	devices_by_id = g_hash_table_new(g_str_hash, g_str_equal);
	for (guint i = 0; i < devices->len; i++) {
		FwupdDevice *dev = g_ptr_array_index(devices, i);
		if (fwupd_device_get_id(dev) == NULL)
			continue;
		g_hash_table_insert(devices_by_id,
				    (gpointer)fwupd_device_get_id(dev),
				    (gpointer)dev);
	}

	/* set the parent on each child */
	for (guint i = 0; i < devices->len; i++) {
		FwupdDevice *dev = g_ptr_array_index(devices, i);
		const gchar *parent_id = fwupd_device_get_parent_id(dev);
		if (parent_id != NULL) {
			FwupdDevice *dev_tmp;
			dev_tmp = g_hash_table_lookup(devices_by_id, parent_id);
			if (dev_tmp != NULL)
				fwupd_device_set_parent(dev, dev_tmp);
		}
	}
}

/**
 * fwupd_device_compare:
 * @self1: (not nullable): a device
 * @self2: (not nullable): a different device
 *
 * Comparison function for comparing two device objects.
 *
 * Returns: negative, 0 or positive
 *
 * Since: 1.1.1
 **/
gint
fwupd_device_compare(FwupdDevice *self1, FwupdDevice *self2)
{
	FwupdDevicePrivate *priv1 = GET_PRIVATE(self1);
	FwupdDevicePrivate *priv2 = GET_PRIVATE(self2);
	g_return_val_if_fail(FWUPD_IS_DEVICE(self1), 0);
	g_return_val_if_fail(FWUPD_IS_DEVICE(self2), 0);
	return g_strcmp0(priv1->id, priv2->id);
}

/**
 * fwupd_device_match_flags:
 * @include: #FwupdDeviceFlags, or %FWUPD_DEVICE_FLAG_NONE
 * @exclude: #FwupdDeviceFlags, or %FWUPD_DEVICE_FLAG_NONE
 *
 * Check if the device flags match.
 *
 * Returns: %TRUE if the device flags match
 *
 * Since: 1.9.3
 **/
gboolean
fwupd_device_match_flags(FwupdDevice *self, FwupdDeviceFlags include, FwupdDeviceFlags exclude)
{
	g_return_val_if_fail(FWUPD_IS_DEVICE(self), FALSE);

	for (guint i = 0; i < 64; i++) {
		FwupdDeviceFlags flag = 1LLU << i;
		if ((include & flag) > 0) {
			if (!fwupd_device_has_flag(self, flag))
				return FALSE;
		}
		if ((exclude & flag) > 0) {
			if (fwupd_device_has_flag(self, flag))
				return FALSE;
		}
	}
	return TRUE;
}

/**
 * fwupd_device_array_filter_flags:
 * @devices: (not nullable) (element-type FwupdDevice): devices
 * @include: #FwupdDeviceFlags, or %FWUPD_DEVICE_FLAG_NONE
 * @exclude: #FwupdDeviceFlags, or %FWUPD_DEVICE_FLAG_NONE
 * @error: (nullable): optional return location for an error
 *
 * Creates an array of new devices that match using fwupd_device_match_flags().
 *
 * Returns: (transfer container) (element-type FwupdDevice): devices
 *
 * Since: 1.9.3
 **/
GPtrArray *
fwupd_device_array_filter_flags(GPtrArray *devices,
				FwupdDeviceFlags include,
				FwupdDeviceFlags exclude,
				GError **error)
{
	g_autoptr(GPtrArray) devices_filtered =
	    g_ptr_array_new_with_free_func((GDestroyNotify)g_object_unref);

	g_return_val_if_fail(devices != NULL, NULL);
	g_return_val_if_fail(error == NULL || *error == NULL, NULL);

	for (guint i = 0; i < devices->len; i++) {
		FwupdDevice *device = g_ptr_array_index(devices, i);
		if (!fwupd_device_match_flags(device, include, exclude))
			continue;
		g_ptr_array_add(devices_filtered, g_object_ref(device));
	}
	if (devices_filtered->len == 0) {
		g_set_error_literal(error, FWUPD_ERROR, FWUPD_ERROR_NOTHING_TO_DO, "no devices");
		return NULL;
	}
	return g_steal_pointer(&devices_filtered);
}

/**
 * fwupd_device_new:
 *
 * Creates a new device.
 *
 * Returns: a new #FwupdDevice
 *
 * Since: 0.9.3
 **/
FwupdDevice *
fwupd_device_new(void)
{
	FwupdDevice *self;
	self = g_object_new(FWUPD_TYPE_DEVICE, NULL);
	return FWUPD_DEVICE(self);
}
