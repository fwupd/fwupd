/*
 * Copyright 2026 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#define G_LOG_DOMAIN "FuTemporaryDirectory"

#include "config.h"

#include "fu-path.h"
#include "fu-temporary-directory.h"

/**
 * FuTemporaryDirectory:
 *
 * An object to create and destroy a temporary directory.
 */

struct _FuTemporaryDirectory {
	GObject parent_instance;
	gchar *path;
};

G_DEFINE_TYPE(FuTemporaryDirectory, fu_temporary_directory, G_TYPE_OBJECT)

/**
 * fu_temporary_directory_get_path:
 * @self: a #FuTemporaryDirectory
 *
 * Gets the path of the temporary directory.
 *
 * Returns: (transfer full): data
 *
 * Since: 2.1.1
 **/
const gchar *
fu_temporary_directory_get_path(FuTemporaryDirectory *self)
{
	g_return_val_if_fail(FU_IS_TEMPORARY_DIRECTORY(self), NULL);
	return self->path;
}

/**
 * fu_temporary_directory_new:
 * @prefix: (nullable): optional prefix
 *
 * Creates a new temporary directory that will be deleted (recursively) when this object is
 * destroyed.
 *
 * Returns: (transfer full): a #FuTemporaryDirectory
 *
 * Since: 2.1.1
 **/
FuTemporaryDirectory *
fu_temporary_directory_new(const gchar *prefix, GError **error)
{
	g_autofree gchar *pattern = NULL;
	g_autoptr(FuTemporaryDirectory) self = g_object_new(FU_TYPE_TEMPORARY_DIRECTORY, NULL);

	pattern = g_strdup_printf("fwupd-%s-XXXXXX", prefix != NULL ? prefix : "tmp");
	self->path = g_dir_make_tmp(pattern, error);
	if (self->path == NULL) {
		fwupd_error_convert(error);
		return NULL;
	}
	return g_steal_pointer(&self);
}

static void
fu_temporary_directory_finalize(GObject *object)
{
	FuTemporaryDirectory *self = FU_TEMPORARY_DIRECTORY(object);
	if (self->path != NULL) {
		g_autoptr(GError) error = NULL;
		if (!fu_path_rmtree(self->path, &error))
			g_warning("failed to delete %s: %s", self->path, error->message);
		g_free(self->path);
	}
	G_OBJECT_CLASS(fu_temporary_directory_parent_class)->finalize(object);
}

static void
fu_temporary_directory_class_init(FuTemporaryDirectoryClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS(klass);
	object_class->finalize = fu_temporary_directory_finalize;
}

static void
fu_temporary_directory_init(FuTemporaryDirectory *self)
{
}
