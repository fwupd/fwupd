/*
 * Copyright 2025 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#define G_LOG_DOMAIN "FuVolumeLocker"

#include "config.h"

#include "fu-volume-locker.h"

/**
 * FuVolumeLocker:
 *
 * Easily unmount a volume when an object goes out of scope.
 *
 * See also: [class@FuVolume]
 */

struct _FuVolumeLocker {
	GObject parent_instance;
	FuVolume *volume;
	gboolean is_open;
};

G_DEFINE_TYPE(FuVolumeLocker, fu_volume_locker, G_TYPE_OBJECT)

/**
 * fu_volume_locker_close:
 * @self: a #FuVolumeLocker
 * @error: (nullable): optional return location for an error
 *
 * Closes the volume before it gets cleaned up.
 *
 * This function can be used to manually unmount a volume managed by a locker,
 * and allows the caller to properly handle the error.
 *
 * Returns: %TRUE for success
 *
 * Since: 2.0.15
 **/
gboolean
fu_volume_locker_close(FuVolumeLocker *self, GError **error)
{
	g_autoptr(GError) error_local = NULL;

	g_return_val_if_fail(FU_IS_VOLUME_LOCKER(self), FALSE);
	g_return_val_if_fail(error == NULL || *error == NULL, FALSE);

	if (!self->is_open)
		return TRUE;
	if (!fu_volume_unmount(self->volume, &error_local)) {
		if (g_error_matches(error_local, FWUPD_ERROR, FWUPD_ERROR_NOT_FOUND)) {
			g_debug("ignoring: %s", error_local->message);
			return TRUE;
		}
		g_propagate_error(error, g_steal_pointer(&error_local));
		return FALSE;
	}

	/* success */
	self->is_open = FALSE;
	return TRUE;
}

static void
fu_volume_locker_finalize(GObject *obj)
{
	FuVolumeLocker *self = FU_VOLUME_LOCKER(obj);

	if (self->is_open) {
		g_autoptr(GError) error_local = NULL;
		if (!fu_volume_unmount(self->volume, &error_local))
			g_warning("failed to close volume: %s", error_local->message);
	}
	if (self->volume != NULL)
		g_object_unref(self->volume);
	G_OBJECT_CLASS(fu_volume_locker_parent_class)->finalize(obj);
}

static void
fu_volume_locker_class_init(FuVolumeLockerClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS(klass);
	object_class->finalize = fu_volume_locker_finalize;
}

static void
fu_volume_locker_init(FuVolumeLocker *self)
{
}

/**
 * fu_volume_locker_new:
 * @volume: a #GObject
 * @error: (nullable): optional return location for an error
 *
 * Locks the volume, mounting it and unmounting it as required. If the volume is
 * already mounted then it is is _not_ unmounted when the locker is closed.
 *
 * Returns: (transfer full): a volume locker if mounted, or %NULL
 *
 * Since: 2.0.15
 **/
FuVolumeLocker *
fu_volume_locker_new(FuVolume *volume, GError **error)
{
	g_autoptr(FuVolumeLocker) self = g_object_new(FU_TYPE_VOLUME_LOCKER, NULL);

	g_return_val_if_fail(FU_IS_VOLUME(volume), NULL);
	g_return_val_if_fail(error == NULL || *error == NULL, NULL);

	/* already open, so NOP */
	if (fu_volume_is_mounted(volume))
		return g_steal_pointer(&self);

	/* open volume */
	if (!fu_volume_mount(volume, error)) {
		g_autoptr(GError) error_local = NULL;
		if (!fu_volume_unmount(volume, &error_local)) {
			if (!g_error_matches(error_local, FWUPD_ERROR, FWUPD_ERROR_NOTHING_TO_DO)) {
				g_debug("ignoring unmount error on aborted mount: %s",
					error_local->message);
			}
		}
		return NULL;
	}

	/* create object */
	self = g_object_new(FU_TYPE_VOLUME_LOCKER, NULL);
	self->is_open = TRUE;
	self->volume = g_object_ref(volume);
	return g_steal_pointer(&self);
}
