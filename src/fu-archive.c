/*
 * Copyright (C) 2018 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#define G_LOG_DOMAIN				"FuArchive"

#include "config.h"

#include <gio/gio.h>
#include <archive_entry.h>
#include <archive.h>

#include "fu-archive.h"

/**
 * SECTION:fu-archive
 * @title: FuArchive
 * @short_description: an in-memory archive decompressor
 */

struct _FuArchive {
	GObject			 parent_instance;
	GHashTable		*entries;
};

G_DEFINE_TYPE (FuArchive, fu_archive, G_TYPE_OBJECT)

static void
fu_archive_finalize (GObject *obj)
{
	FuArchive *self = FU_ARCHIVE (obj);

	g_hash_table_unref (self->entries);
	G_OBJECT_CLASS (fu_archive_parent_class)->finalize (obj);
}

static void
fu_archive_class_init (FuArchiveClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	object_class->finalize = fu_archive_finalize;
}

static void
fu_archive_init (FuArchive *self)
{
	self->entries = g_hash_table_new_full (g_str_hash, g_str_equal,
					       g_free, (GDestroyNotify) g_bytes_unref);
}

/**
 * fu_archive_lookup_by_fn:
 * @self: A #FuArchive
 * @fn: A filename
 * @error: A #GError, or %NULL
 *
 * Finds the blob referenced by filename
 *
 * Returns: (transfer none): a #GBytes, or %NULL if the filename was not found
 *
 * Since: 1.2.2
 **/
GBytes *
fu_archive_lookup_by_fn (FuArchive *self, const gchar *fn, GError **error)
{
	GBytes *fw;

	g_return_val_if_fail (FU_IS_ARCHIVE (self), NULL);
	g_return_val_if_fail (fn != NULL, NULL);
	g_return_val_if_fail (error == NULL || *error == NULL, NULL);

	fw = g_hash_table_lookup (self->entries, fn);
	if (fw == NULL) {
		g_set_error (error,
			     G_IO_ERROR,
			     G_IO_ERROR_NOT_FOUND,
			     "no blob for %s", fn);
	}
	return fw;
}

/**
 * fu_archive_iterate:
 * @self: A #FuArchive
 * @callback: (scope call): A #FuArchiveIterateFunc.
 * @user_data: User data.
 * @error: A #GError, or %NULL
 *
 * Iterates over the archive contents, calling the given function for each
 * of the files found. If any @callback returns %FALSE scanning is aborted.
 *
 * Returns: True if no @callback returned FALSE
 *
 * Since: 1.3.4
 */
gboolean
fu_archive_iterate (FuArchive *self,
		    FuArchiveIterateFunc callback,
		    gpointer user_data,
		    GError **error)
{
	GHashTableIter iter;
	gpointer key, value;

	g_return_val_if_fail (FU_IS_ARCHIVE (self), FALSE);
	g_return_val_if_fail (callback != NULL, FALSE);

	g_hash_table_iter_init (&iter, self->entries);
	while (g_hash_table_iter_next (&iter, &key, &value)) {
		if (!callback (self, (const gchar *)key, (GBytes *)value, user_data, error))
			return FALSE;
	}
	return TRUE;
}

/* workaround the struct types of libarchive */
typedef struct archive _archive_read_ctx;

static void
_archive_read_ctx_free (_archive_read_ctx *arch)
{
	archive_read_close (arch);
	archive_read_free (arch);
}

G_DEFINE_AUTOPTR_CLEANUP_FUNC(_archive_read_ctx, _archive_read_ctx_free)

static gboolean
fu_archive_load (FuArchive *self, GBytes *blob, FuArchiveFlags flags, GError **error)
{
	int r;
	g_autoptr(_archive_read_ctx) arch = NULL;

	/* decompress anything matching either glob */
	arch = archive_read_new ();
	if (arch == NULL) {
		g_set_error_literal (error,
				     G_IO_ERROR,
				     G_IO_ERROR_NOT_SUPPORTED,
				     "libarchive startup failed");
		return FALSE;
	}
	archive_read_support_format_all (arch);
	archive_read_support_filter_all (arch);
	r = archive_read_open_memory (arch,
				      (void *) g_bytes_get_data (blob, NULL),
				      (size_t) g_bytes_get_size (blob));
	if (r != 0) {
		g_set_error (error,
			     G_IO_ERROR,
			     G_IO_ERROR_NOT_SUPPORTED,
			     "cannot open: %s",
			     archive_error_string (arch));
		return FALSE;
	}
	while (TRUE) {
		const gchar *fn;
		gint64 bufsz;
		gssize rc;
		struct archive_entry *entry;
		g_autofree gchar *fn_key = NULL;
		g_autofree guint8 *buf = NULL;

		r = archive_read_next_header (arch, &entry);
		if (r == ARCHIVE_EOF)
			break;
		if (r != ARCHIVE_OK) {
			g_set_error (error,
				     G_IO_ERROR,
				     G_IO_ERROR_FAILED,
				     "cannot read header: %s",
				     archive_error_string (arch));
			return FALSE;
		}

		/* only extract if valid */
		fn = archive_entry_pathname (entry);
		if (fn == NULL)
			continue;
		bufsz = archive_entry_size (entry);
		if (bufsz > 1024 * 1024 * 1024) {
			g_set_error_literal (error,
					     G_IO_ERROR,
					     G_IO_ERROR_FAILED,
					     "cannot read huge files");
			return FALSE;
		}
		buf = g_malloc (bufsz);
		rc = archive_read_data (arch, buf, (gsize) bufsz);
		if (rc < 0) {
			g_set_error (error,
				     G_IO_ERROR,
				     G_IO_ERROR_FAILED,
				     "cannot read data: %s",
				     archive_error_string (arch));
			return FALSE;
		}
		if (rc != bufsz) {
			g_set_error (error,
				     G_IO_ERROR,
				     G_IO_ERROR_FAILED,
				     "read %" G_GSSIZE_FORMAT " of %" G_GINT64_FORMAT,
				     rc, bufsz);
			return FALSE;
		}
		if (flags & FU_ARCHIVE_FLAG_IGNORE_PATH) {
			fn_key = g_path_get_basename (fn);
		} else {
			fn_key = g_strdup (fn);
		}
		g_debug ("adding %s [%" G_GINT64_FORMAT "]", fn_key, bufsz);
		g_hash_table_insert (self->entries,
				     g_steal_pointer (&fn_key),
				     g_bytes_new_take (g_steal_pointer (&buf), bufsz));
	}

	/* success */
	return TRUE;
}

/**
 * fu_archive_new:
 * @data: A #GBytes
 * @flags: A #FuArchiveFlags, e.g. %FU_ARCHIVE_FLAG_NONE
 * @error: A #GError, or %NULL
 *
 * Parses @data as an archive and decompresses all files to memory blobs.
 *
 * Returns: a #FuArchive, or %NULL if the archive was invalid in any way.
 *
 * Since: 1.2.2
 **/
FuArchive *
fu_archive_new (GBytes *data, FuArchiveFlags flags, GError **error)
{
	g_autoptr(FuArchive) self = g_object_new (FU_TYPE_ARCHIVE, NULL);
	g_return_val_if_fail (data != NULL, NULL);
	g_return_val_if_fail (error == NULL || *error == NULL, NULL);
	if (!fu_archive_load (self, data, flags, error))
		return NULL;
	return g_steal_pointer (&self);
}
