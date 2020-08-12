/*
 * Copyright (C) 2018-2020 Richard Hughes <richard@hughsie.com>
 * Copyright (C) 2019 Mario Limonciello <mario.limonciello@dell.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#define G_LOG_DOMAIN				"FuVolume"

#include "config.h"

#include <gio/gio.h>

#include "fwupd-error.h"

#include "fu-volume-private.h"

/**
 * SECTION:fu-volume
 * @title: FuVolume
 * @short_description: Volume abstraction that uses UDisks
 */

struct _FuVolume {
	GObject			 parent_instance;
	GDBusProxy		*proxy;
	gchar			*mount_path;	/* only when mounted ourselves */
};

G_DEFINE_TYPE (FuVolume, fu_volume, G_TYPE_OBJECT)

static void
fu_volume_finalize (GObject *obj)
{
	FuVolume *self = FU_VOLUME (obj);
	g_free (self->mount_path);
	if (self->proxy != NULL)
		g_object_unref (self->proxy);
	G_OBJECT_CLASS (fu_volume_parent_class)->finalize (obj);
}

static void
fu_volume_class_init (FuVolumeClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	object_class->finalize = fu_volume_finalize;
}

static void
fu_volume_init (FuVolume *self)
{
}

/**
 * fu_volume_get_id:
 * @self: a @FuVolume
 *
 * Gets the D-Bus path of the mount point.
 *
 * Returns: string ID, or %NULL
 *
 * Since: 1.4.6
 **/
const gchar *
fu_volume_get_id (FuVolume *self)
{
	g_return_val_if_fail (FU_IS_VOLUME (self), NULL);
	return g_dbus_proxy_get_object_path (self->proxy);
}

/**
 * fu_volume_get_mount_point:
 * @self: a @FuVolume
 *
 * Gets the location of the volume mount point.
 *
 * Returns: UNIX path, or %NULL
 *
 * Since: 1.4.6
 **/
gchar *
fu_volume_get_mount_point (FuVolume *self)
{
	const gchar **mountpoints = NULL;
	g_autoptr(GVariant) val = NULL;
	g_autoptr(GError) error_local = NULL;

	g_return_val_if_fail (FU_IS_VOLUME (self), NULL);

	/* we mounted it */
	if (self->mount_path != NULL)
		return g_strdup (self->mount_path);

	/* something else mounted it */
	val = g_dbus_proxy_get_cached_property (self->proxy, "MountPoints");
	if (val == NULL)
		return NULL;
	mountpoints = g_variant_get_bytestring_array (val, NULL);
	return g_strdup (mountpoints[0]);
}

/**
 * fu_volume_check_free_space:
 * @self: a @FuVolume
 * @required: size in bytes
 * @error: A #GError, or %NULL
 *
 * Checks the volume for required space.
 *
 * Returns: %TRUE for success
 *
 * Since: 1.4.6
 **/
gboolean
fu_volume_check_free_space (FuVolume *self, guint64 required, GError **error)
{
	guint64 fs_free;
	g_autofree gchar *path = NULL;
	g_autoptr(GFile) file = NULL;
	g_autoptr(GFileInfo) info = NULL;

	g_return_val_if_fail (FU_IS_VOLUME (self), FALSE);

	/* skip the checks for unmounted disks */
	path = fu_volume_get_mount_point (self);
	if (path == NULL)
		return TRUE;

	file = g_file_new_for_path (path);
	info = g_file_query_filesystem_info (file,
					     G_FILE_ATTRIBUTE_FILESYSTEM_FREE,
					     NULL, error);
	if (info == NULL)
		return FALSE;
	fs_free = g_file_info_get_attribute_uint64 (info, G_FILE_ATTRIBUTE_FILESYSTEM_FREE);
	if (fs_free < required) {
		g_autofree gchar *str_free = g_format_size (fs_free);
		g_autofree gchar *str_reqd = g_format_size (required);
		g_set_error (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_NOT_SUPPORTED,
			     "%s does not have sufficient space, required %s, got %s",
			     path, str_reqd, str_free);
		return FALSE;
	}
	return TRUE;
}

/**
 * fu_volume_is_mounted:
 * @self: a @FuVolume
 *
 * Checks if the VOLUME is already mounted.
 *
 * Returns: %TRUE for success
 *
 * Since: 1.4.6
 **/
gboolean
fu_volume_is_mounted (FuVolume *self)
{
	g_autofree gchar *mount_point = NULL;
	g_return_val_if_fail (FU_IS_VOLUME (self), FALSE);
	mount_point = fu_volume_get_mount_point (self);
	return mount_point != NULL;
}

/**
 * fu_volume_mount:
 * @self: a @FuVolume
 * @error: A #GError, or %NULL
 *
 * Mounts the VOLUME ready for use.
 *
 * Returns: %TRUE for success
 *
 * Since: 1.4.6
 **/
gboolean
fu_volume_mount (FuVolume *self, GError **error)
{
	GVariantBuilder builder;
	g_autoptr(GVariant) val = NULL;

	g_return_val_if_fail (FU_IS_VOLUME (self), FALSE);

	/* device from the self tests */
	if (self->proxy == NULL)
		return TRUE;

	g_debug ("mounting %s", fu_volume_get_id (self));
	g_variant_builder_init (&builder, G_VARIANT_TYPE_VARDICT);
	val = g_dbus_proxy_call_sync (self->proxy,
				      "Mount", g_variant_new ("(a{sv})", &builder),
				      G_DBUS_CALL_FLAGS_NONE,
				      -1, NULL, error);
	if (val == NULL)
		return FALSE;
	g_variant_get (val, "(s)", &self->mount_path);
	return TRUE;
}

/**
 * fu_volume_unmount:
 * @self: a @FuVolume
 * @error: A #GError, or %NULL
 *
 * Unmounts the volume after use.
 *
 * Returns: %TRUE for success
 *
 * Since: 1.4.6
 **/
gboolean
fu_volume_unmount (FuVolume *self, GError **error)
{
	GVariantBuilder builder;
	g_autoptr(GVariant) val = NULL;

	g_return_val_if_fail (FU_IS_VOLUME (self), FALSE);

	/* device from the self tests */
	if (self->proxy == NULL)
		return TRUE;

	g_debug ("unmounting %s", fu_volume_get_id (self));
	g_variant_builder_init (&builder, G_VARIANT_TYPE_VARDICT);
	val = g_dbus_proxy_call_sync (self->proxy,
				      "Unmount",
				      g_variant_new ("(a{sv})", &builder),
				      G_DBUS_CALL_FLAGS_NONE,
				      -1, NULL, error);
	if (val == NULL)
		return FALSE;
	g_free (self->mount_path);
	self->mount_path = NULL;
	return TRUE;
}

/**
 * fu_volume_locker:
 * @self: a @FuVolume
 * @error: A #GError, or %NULL
 *
 * Locks the volume, mounting it and unmounting it as required. If the volume is
 * already mounted then it is is _not_ unmounted when the locker is closed.
 *
 * Returns: (transfer full): a #FuDeviceLocker for success, or %NULL
 *
 * Since: 1.4.6
 **/
FuDeviceLocker *
fu_volume_locker (FuVolume *self, GError **error)
{
	/* already open, so NOP */
	if (fu_volume_is_mounted (self))
		return g_object_new (FU_TYPE_DEVICE_LOCKER, NULL);
	return fu_device_locker_new_full (self,
					  (FuDeviceLockerFunc) fu_volume_mount,
					  (FuDeviceLockerFunc) fu_volume_unmount,
					  error);
}

/* private */
FuVolume *
fu_volume_new_from_proxy (GDBusProxy *proxy)
{
	g_autoptr(FuVolume) self = g_object_new (FU_TYPE_VOLUME, NULL);
	g_return_val_if_fail (proxy != NULL, NULL);
	g_set_object (&self->proxy, proxy);
	return g_steal_pointer (&self);
}

/* private */
FuVolume *
fu_volume_new_from_mount_path (const gchar *mount_path)
{
	g_autoptr(FuVolume) self = g_object_new (FU_TYPE_VOLUME, NULL);
	g_return_val_if_fail (mount_path != NULL, NULL);
	self->mount_path = g_strdup (mount_path);
	return g_steal_pointer (&self);
}
