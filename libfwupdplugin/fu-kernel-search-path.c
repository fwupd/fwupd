/*
 * Copyright 2017 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#define G_LOG_DOMAIN "FuKernel"

#include "config.h"

#include "fu-kernel-search-path-private.h"
#include "fu-path.h"

/**
 * FuKernelSearchPathLocker:
 *
 * Easily reset the firmware search path.
 */

struct _FuKernelSearchPathLocker {
	GObject parent_instance;
	gchar *old_path;
};

G_DEFINE_TYPE(FuKernelSearchPathLocker, fu_kernel_search_path_locker, G_TYPE_OBJECT)

/* private */
gchar *
fu_kernel_search_path_get_current(GError **error)
{
	gsize sz = 0;
	g_autofree gchar *sys_fw_search_path = NULL;
	g_autofree gchar *contents = NULL;

	sys_fw_search_path = fu_path_from_kind(FU_PATH_KIND_FIRMWARE_SEARCH);
	if (!g_file_get_contents(sys_fw_search_path, &contents, &sz, error))
		return NULL;

	/* sanity check */
	if (contents == NULL || sz == 0) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INTERNAL,
			    "failed to get firmware search path from %s",
			    sys_fw_search_path);
		return NULL;
	}

	/* remove newline character */
	if (contents[sz - 1] == '\n')
		contents[sz - 1] = 0;

	g_debug("read firmware search path (%" G_GSIZE_FORMAT "): %s", sz, contents);
	return g_steal_pointer(&contents);
}

static gboolean
fu_kernel_search_path_set_current(const gchar *path, GError **error)
{
	g_autofree gchar *sys_fw_search_path_prm = NULL;

	g_return_val_if_fail(path != NULL, FALSE);
	g_return_val_if_fail(strlen(path) < PATH_MAX, FALSE);

	g_debug("writing firmware search path (%" G_GSIZE_FORMAT "): %s", strlen(path), path);
	sys_fw_search_path_prm = fu_path_from_kind(FU_PATH_KIND_FIRMWARE_SEARCH);
	return g_file_set_contents_full(sys_fw_search_path_prm,
					path,
					strlen(path),
					G_FILE_SET_CONTENTS_NONE,
					0644,
					error);
}

static gboolean
fu_kernel_search_path_locker_close(FuKernelSearchPathLocker *self, GError **error)
{
	if (self->old_path == NULL)
		return TRUE;
	if (!fu_kernel_search_path_set_current(self->old_path, error))
		return FALSE;
	g_clear_pointer(&self->old_path, g_free);
	return TRUE;
}

/**
 * fu_kernel_search_path_locker_new:
 * @path: the new devivce path
 * @error: (nullable): optional return location for an error
 *
 * Sets the kernel firmware search path. When the #FuKernelSearchPathLocker is deallocated path
 * is restored to the previous value.
 *
 * This object is typically called using g_autoptr() but the device can also be
 * manually closed using g_clear_object().
 *
 * Returns: (transfer full): a #FuKernelSearchPathLocker, or %NULL on error
 *
 * Since: 2.0.6
 **/
FuKernelSearchPathLocker *
fu_kernel_search_path_locker_new(const gchar *path, GError **error)
{
	g_autofree gchar *old_path = NULL;
	g_autoptr(FuKernelSearchPathLocker) self = NULL;

	g_return_val_if_fail(path != NULL, NULL);
	g_return_val_if_fail(error == NULL || *error == NULL, NULL);

	/* create object */
	self = g_object_new(FU_TYPE_KERNEL_SEARCH_PATH_LOCKER, NULL);
	old_path = fu_kernel_search_path_get_current(error);
	if (old_path == NULL)
		return NULL;

	/* set the new path if different */
	if (g_strcmp0(self->old_path, path) != 0) {
		self->old_path = g_steal_pointer(&old_path);
		if (!fu_kernel_search_path_set_current(path, error))
			return NULL;
	}

	/* success */
	return g_steal_pointer(&self);
}

static void
fu_kernel_search_path_locker_dispose(GObject *obj)
{
	FuKernelSearchPathLocker *self = FU_KERNEL_SEARCH_PATH_LOCKER(obj);
	if (self->old_path != NULL) {
		g_autoptr(GError) error = NULL;
		if (!fu_kernel_search_path_locker_close(self, &error))
			g_warning("failed to restore path: %s", error->message);
	}
	G_OBJECT_CLASS(fu_kernel_search_path_locker_parent_class)->dispose(obj);
}

static void
fu_kernel_search_path_locker_finalize(GObject *obj)
{
	FuKernelSearchPathLocker *self = FU_KERNEL_SEARCH_PATH_LOCKER(obj);
	g_free(self->old_path);
	G_OBJECT_CLASS(fu_kernel_search_path_locker_parent_class)->finalize(obj);
}

static void
fu_kernel_search_path_locker_class_init(FuKernelSearchPathLockerClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS(klass);
	object_class->dispose = fu_kernel_search_path_locker_dispose;
	object_class->finalize = fu_kernel_search_path_locker_finalize;
}

static void
fu_kernel_search_path_locker_init(FuKernelSearchPathLocker *self)
{
}
