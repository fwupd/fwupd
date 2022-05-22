/*
 * Copyright (C) 2018 Richard Hughes <richard@hughsie.com>
 * Copyright (C) 2022 Gaël PORTAY <gael.portay@collabora.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#define G_LOG_DOMAIN "FuArchive"

#include "config.h"

#include <gio/gio.h>

#ifdef HAVE_LIBARCHIVE
#include <archive.h>
#include <archive_entry.h>
#endif

#include "fwupd-error.h"

#include "fu-archive.h"

/**
 * FuArchive:
 *
 * An in-memory archive decompressor
 */

struct _FuArchive {
	GObject parent_instance;
	GHashTable *entries; /* str:GBytes */
};

G_DEFINE_TYPE(FuArchive, fu_archive, G_TYPE_OBJECT)

/**
 * fu_archive_format_to_string:
 * @format: a format, e.g. %FU_ARCHIVE_FORMAT_ZIP
 *
 * Converts an enumerated format to a string.
 *
 * Returns: identifier string
 *
 * Since: 1.8.1
 **/
const gchar *
fu_archive_format_to_string(FuArchiveFormat format)
{
	if (format == FU_ARCHIVE_FORMAT_UNKNOWN)
		return "unknown";
	if (format == FU_ARCHIVE_FORMAT_CPIO)
		return "cpio";
	if (format == FU_ARCHIVE_FORMAT_SHAR)
		return "shar";
	if (format == FU_ARCHIVE_FORMAT_TAR)
		return "tar";
	if (format == FU_ARCHIVE_FORMAT_USTAR)
		return "ustar";
	if (format == FU_ARCHIVE_FORMAT_PAX)
		return "pax";
	if (format == FU_ARCHIVE_FORMAT_GNUTAR)
		return "gnutar";
	if (format == FU_ARCHIVE_FORMAT_ISO9660)
		return "iso9660";
	if (format == FU_ARCHIVE_FORMAT_ZIP)
		return "zip";
	if (format == FU_ARCHIVE_FORMAT_AR)
		return "ar";
	if (format == FU_ARCHIVE_FORMAT_AR_SVR4)
		return "ar-svr4";
	if (format == FU_ARCHIVE_FORMAT_MTREE)
		return "mtree";
	if (format == FU_ARCHIVE_FORMAT_RAW)
		return "raw";
	if (format == FU_ARCHIVE_FORMAT_XAR)
		return "xar";
	if (format == FU_ARCHIVE_FORMAT_7ZIP)
		return "7zip";
	if (format == FU_ARCHIVE_FORMAT_WARC)
		return "warc";
	return NULL;
}

/**
 * fu_archive_format_from_string:
 * @format: (nullable): a string, e.g. `zip`
 *
 * Converts a string to an enumerated format.
 *
 * Returns: enumerated value
 *
 * Since: 1.8.1
 **/
FuArchiveFormat
fu_archive_format_from_string(const gchar *format)
{
	if (g_strcmp0(format, "unknown") == 0)
		return FU_ARCHIVE_FORMAT_UNKNOWN;
	if (g_strcmp0(format, "cpio") == 0)
		return FU_ARCHIVE_FORMAT_CPIO;
	if (g_strcmp0(format, "shar") == 0)
		return FU_ARCHIVE_FORMAT_SHAR;
	if (g_strcmp0(format, "tar") == 0)
		return FU_ARCHIVE_FORMAT_TAR;
	if (g_strcmp0(format, "ustar") == 0)
		return FU_ARCHIVE_FORMAT_USTAR;
	if (g_strcmp0(format, "pax") == 0)
		return FU_ARCHIVE_FORMAT_PAX;
	if (g_strcmp0(format, "gnutar") == 0)
		return FU_ARCHIVE_FORMAT_GNUTAR;
	if (g_strcmp0(format, "iso9660") == 0)
		return FU_ARCHIVE_FORMAT_ISO9660;
	if (g_strcmp0(format, "zip") == 0)
		return FU_ARCHIVE_FORMAT_ZIP;
	if (g_strcmp0(format, "ar") == 0)
		return FU_ARCHIVE_FORMAT_AR;
	if (g_strcmp0(format, "ar-svr4") == 0)
		return FU_ARCHIVE_FORMAT_AR_SVR4;
	if (g_strcmp0(format, "mtree") == 0)
		return FU_ARCHIVE_FORMAT_MTREE;
	if (g_strcmp0(format, "raw") == 0)
		return FU_ARCHIVE_FORMAT_RAW;
	if (g_strcmp0(format, "xar") == 0)
		return FU_ARCHIVE_FORMAT_XAR;
	if (g_strcmp0(format, "7zip") == 0)
		return FU_ARCHIVE_FORMAT_7ZIP;
	if (g_strcmp0(format, "warc") == 0)
		return FU_ARCHIVE_FORMAT_WARC;
	return FU_ARCHIVE_FORMAT_UNKNOWN;
}

/**
 * fu_archive_compression_to_string:
 * @compression: a compression, e.g. %FU_ARCHIVE_COMPRESSION_ZIP
 *
 * Converts an enumerated compression to a string.
 *
 * Returns: identifier string
 *
 * Since: 1.8.1
 **/
const gchar *
fu_archive_compression_to_string(FuArchiveCompression compression)
{
	if (compression == FU_ARCHIVE_COMPRESSION_UNKNOWN)
		return "unknown";
	if (compression == FU_ARCHIVE_COMPRESSION_NONE)
		return "none";
	if (compression == FU_ARCHIVE_COMPRESSION_GZIP)
		return "gzip";
	if (compression == FU_ARCHIVE_COMPRESSION_BZIP2)
		return "bzip2";
	if (compression == FU_ARCHIVE_COMPRESSION_COMPRESS)
		return "compress";
	if (compression == FU_ARCHIVE_COMPRESSION_LZMA)
		return "lzma";
	if (compression == FU_ARCHIVE_COMPRESSION_XZ)
		return "xz";
	if (compression == FU_ARCHIVE_COMPRESSION_UU)
		return "uuencode";
	if (compression == FU_ARCHIVE_COMPRESSION_LZIP)
		return "lzip";
	if (compression == FU_ARCHIVE_COMPRESSION_LRZIP)
		return "lrzip";
	if (compression == FU_ARCHIVE_COMPRESSION_LZOP)
		return "lzop";
	if (compression == FU_ARCHIVE_COMPRESSION_GRZIP)
		return "grzip";
	if (compression == FU_ARCHIVE_COMPRESSION_LZ4)
		return "lz4";
	if (compression == FU_ARCHIVE_COMPRESSION_ZSTD)
		return "zstd";
	return NULL;
}

/**
 * fu_archive_compression_from_string:
 * @compression: (nullable): a string, e.g. `zip`
 *
 * Converts a string to an enumerated compression.
 *
 * Returns: enumerated value
 *
 * Since: 1.8.1
 **/
FuArchiveCompression
fu_archive_compression_from_string(const gchar *compression)
{
	if (g_strcmp0(compression, "unknown") == 0)
		return FU_ARCHIVE_COMPRESSION_UNKNOWN;
	if (g_strcmp0(compression, "none") == 0)
		return FU_ARCHIVE_COMPRESSION_NONE;
	if (g_strcmp0(compression, "gzip") == 0)
		return FU_ARCHIVE_COMPRESSION_GZIP;
	if (g_strcmp0(compression, "bzip2") == 0)
		return FU_ARCHIVE_COMPRESSION_BZIP2;
	if (g_strcmp0(compression, "compress") == 0)
		return FU_ARCHIVE_COMPRESSION_COMPRESS;
	if (g_strcmp0(compression, "lzma") == 0)
		return FU_ARCHIVE_COMPRESSION_LZMA;
	if (g_strcmp0(compression, "xz") == 0)
		return FU_ARCHIVE_COMPRESSION_XZ;
	if (g_strcmp0(compression, "uuencode") == 0)
		return FU_ARCHIVE_COMPRESSION_UU;
	if (g_strcmp0(compression, "lzip") == 0)
		return FU_ARCHIVE_COMPRESSION_LZIP;
	if (g_strcmp0(compression, "lrzip") == 0)
		return FU_ARCHIVE_COMPRESSION_LRZIP;
	if (g_strcmp0(compression, "lzop") == 0)
		return FU_ARCHIVE_COMPRESSION_LZOP;
	if (g_strcmp0(compression, "grzip") == 0)
		return FU_ARCHIVE_COMPRESSION_GRZIP;
	if (g_strcmp0(compression, "lz4") == 0)
		return FU_ARCHIVE_COMPRESSION_LZ4;
	if (g_strcmp0(compression, "zstd") == 0)
		return FU_ARCHIVE_COMPRESSION_ZSTD;
	return FU_ARCHIVE_COMPRESSION_UNKNOWN;
}

static void
fu_archive_finalize(GObject *obj)
{
	FuArchive *self = FU_ARCHIVE(obj);

	g_hash_table_unref(self->entries);
	G_OBJECT_CLASS(fu_archive_parent_class)->finalize(obj);
}

static void
fu_archive_class_init(FuArchiveClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS(klass);
	object_class->finalize = fu_archive_finalize;
}

static void
fu_archive_init(FuArchive *self)
{
	self->entries =
	    g_hash_table_new_full(g_str_hash, g_str_equal, g_free, (GDestroyNotify)g_bytes_unref);
}

/**
 * fu_archive_add_entry:
 * @self: a #FuArchive
 * @fn: (not nullable): a filename
 * @blob: (not nullable): a #GBytes
 *
 * Adds, or replaces an entry to an archive.
 *
 * Since: 1.8.1
 **/
void
fu_archive_add_entry(FuArchive *self, const gchar *fn, GBytes *blob)
{
	g_return_if_fail(FU_IS_ARCHIVE(self));
	g_return_if_fail(fn != NULL);
	g_return_if_fail(blob != NULL);
	g_hash_table_insert(self->entries, g_strdup(fn), g_bytes_ref(blob));
}

/**
 * fu_archive_lookup_by_fn:
 * @self: a #FuArchive
 * @fn: a filename
 * @error: (nullable): optional return location for an error
 *
 * Finds the blob referenced by filename
 *
 * Returns: (transfer none): a #GBytes, or %NULL if the filename was not found
 *
 * Since: 1.2.2
 **/
GBytes *
fu_archive_lookup_by_fn(FuArchive *self, const gchar *fn, GError **error)
{
	GBytes *bytes;

	g_return_val_if_fail(FU_IS_ARCHIVE(self), NULL);
	g_return_val_if_fail(fn != NULL, NULL);
	g_return_val_if_fail(error == NULL || *error == NULL, NULL);

	bytes = g_hash_table_lookup(self->entries, fn);
	if (bytes == NULL) {
		g_set_error(error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND, "no blob for %s", fn);
	}
	return bytes;
}

/**
 * fu_archive_iterate:
 * @self: a #FuArchive
 * @callback: (scope call): a #FuArchiveIterateFunc.
 * @user_data: user data
 * @error: (nullable): optional return location for an error
 *
 * Iterates over the archive contents, calling the given function for each
 * of the files found. If any @callback returns %FALSE scanning is aborted.
 *
 * Returns: True if no @callback returned FALSE
 *
 * Since: 1.3.4
 */
gboolean
fu_archive_iterate(FuArchive *self,
		   FuArchiveIterateFunc callback,
		   gpointer user_data,
		   GError **error)
{
	GHashTableIter iter;
	gpointer key, value;

	g_return_val_if_fail(FU_IS_ARCHIVE(self), FALSE);
	g_return_val_if_fail(callback != NULL, FALSE);
	g_return_val_if_fail(error == NULL || *error == NULL, FALSE);

	g_hash_table_iter_init(&iter, self->entries);
	while (g_hash_table_iter_next(&iter, &key, &value)) {
		if (!callback(self, (const gchar *)key, (GBytes *)value, user_data, error))
			return FALSE;
	}
	return TRUE;
}

#ifdef HAVE_LIBARCHIVE
/* workaround the struct types of libarchive */
typedef struct archive _archive_read_ctx;

static void
_archive_read_ctx_free(_archive_read_ctx *arch)
{
	archive_read_close(arch);
	archive_read_free(arch);
}

G_DEFINE_AUTOPTR_CLEANUP_FUNC(_archive_read_ctx, _archive_read_ctx_free)

typedef struct archive _archive_write_ctx;

static void
_archive_write_ctx_free(_archive_write_ctx *arch)
{
	archive_write_close(arch);
	archive_write_free(arch);
}

G_DEFINE_AUTOPTR_CLEANUP_FUNC(_archive_write_ctx, _archive_write_ctx_free)

typedef struct archive_entry _archive_entry_ctx;

static void
_archive_entry_ctx_free(_archive_entry_ctx *entry)
{
	archive_entry_free(entry);
}

G_DEFINE_AUTOPTR_CLEANUP_FUNC(_archive_entry_ctx, _archive_entry_ctx_free)

static void
fu_archive_set_format(_archive_write_ctx *arch, FuArchiveFormat format)
{
	if (format == FU_ARCHIVE_FORMAT_CPIO)
		archive_write_set_format_cpio(arch);
	if (format == FU_ARCHIVE_FORMAT_SHAR)
		archive_write_set_format_shar(arch);
	if (format == FU_ARCHIVE_FORMAT_TAR)
		archive_write_set_format_pax_restricted(arch);
	if (format == FU_ARCHIVE_FORMAT_USTAR)
		archive_write_set_format_ustar(arch);
	if (format == FU_ARCHIVE_FORMAT_PAX)
		archive_write_set_format_pax(arch);
	if (format == FU_ARCHIVE_FORMAT_GNUTAR)
		archive_write_set_format_gnutar(arch);
	if (format == FU_ARCHIVE_FORMAT_ISO9660)
		archive_write_set_format_iso9660(arch);
	if (format == FU_ARCHIVE_FORMAT_ZIP)
		archive_write_set_format_zip(arch);
	if (format == FU_ARCHIVE_FORMAT_AR)
		archive_write_set_format_ar_bsd(arch);
	if (format == FU_ARCHIVE_FORMAT_AR_SVR4)
		archive_write_set_format_ar_svr4(arch);
	if (format == FU_ARCHIVE_FORMAT_MTREE)
		archive_write_set_format_mtree(arch);
	if (format == FU_ARCHIVE_FORMAT_RAW)
		archive_write_set_format_raw(arch);
	if (format == FU_ARCHIVE_FORMAT_XAR)
		archive_write_set_format_xar(arch);
	if (format == FU_ARCHIVE_FORMAT_7ZIP)
		archive_write_set_format_7zip(arch);
	if (format == FU_ARCHIVE_FORMAT_WARC)
		archive_write_set_format_warc(arch);
}

static void
fu_archive_set_compression(_archive_write_ctx *arch, FuArchiveCompression compression)
{
	if (compression == FU_ARCHIVE_COMPRESSION_BZIP2)
		archive_write_add_filter_bzip2(arch);
	if (compression == FU_ARCHIVE_COMPRESSION_COMPRESS)
		archive_write_add_filter_compress(arch);
	if (compression == FU_ARCHIVE_COMPRESSION_GRZIP)
		archive_write_add_filter_grzip(arch);
	if (compression == FU_ARCHIVE_COMPRESSION_GZIP)
		archive_write_add_filter_gzip(arch);
	if (compression == FU_ARCHIVE_COMPRESSION_LRZIP)
		archive_write_add_filter_lrzip(arch);
	if (compression == FU_ARCHIVE_COMPRESSION_LZ4)
		archive_write_add_filter_lz4(arch);
	if (compression == FU_ARCHIVE_COMPRESSION_LZIP)
		archive_write_add_filter_lzip(arch);
	if (compression == FU_ARCHIVE_COMPRESSION_LZMA)
		archive_write_add_filter_lzma(arch);
	if (compression == FU_ARCHIVE_COMPRESSION_LZOP)
		archive_write_add_filter_lzop(arch);
	if (compression == FU_ARCHIVE_COMPRESSION_UU)
		archive_write_add_filter_uuencode(arch);
	if (compression == FU_ARCHIVE_COMPRESSION_XZ)
		archive_write_add_filter_xz(arch);
#ifdef HAVE_LIBARCHIVE_WRITE_ADD_COMPRESSION_ZSTD
	if (compression == FU_ARCHIVE_COMPRESSION_ZSTD)
		archive_write_add_filter_zstd(arch);
#endif
}
#endif

static gboolean
fu_archive_load(FuArchive *self, GBytes *blob, FuArchiveFlags flags, GError **error)
{
#ifdef HAVE_LIBARCHIVE
	int r;
	g_autoptr(_archive_read_ctx) arch = NULL;

	/* decompress anything matching either glob */
	arch = archive_read_new();
	if (arch == NULL) {
		g_set_error_literal(error,
				    G_IO_ERROR,
				    G_IO_ERROR_NOT_SUPPORTED,
				    "libarchive startup failed");
		return FALSE;
	}
	archive_read_support_format_all(arch);
	archive_read_support_filter_all(arch);
	r = archive_read_open_memory(arch,
				     (void *)g_bytes_get_data(blob, NULL),
				     (size_t)g_bytes_get_size(blob));
	if (r != 0) {
		g_set_error(error,
			    G_IO_ERROR,
			    G_IO_ERROR_NOT_SUPPORTED,
			    "cannot open: %s",
			    archive_error_string(arch));
		return FALSE;
	}
	while (TRUE) {
		const gchar *fn;
		gint64 bufsz;
		gssize rc;
		struct archive_entry *entry;
		g_autofree gchar *fn_key = NULL;
		g_autofree guint8 *buf = NULL;
		g_autoptr(GBytes) bytes = NULL;

		r = archive_read_next_header(arch, &entry);
		if (r == ARCHIVE_EOF)
			break;
		if (r != ARCHIVE_OK) {
			g_set_error(error,
				    G_IO_ERROR,
				    G_IO_ERROR_FAILED,
				    "cannot read header: %s",
				    archive_error_string(arch));
			return FALSE;
		}

		/* only extract if valid */
		fn = archive_entry_pathname(entry);
		if (fn == NULL)
			continue;
		bufsz = archive_entry_size(entry);
		if (bufsz > 1024 * 1024 * 1024) {
			g_set_error_literal(error,
					    G_IO_ERROR,
					    G_IO_ERROR_FAILED,
					    "cannot read huge files");
			return FALSE;
		}
		buf = g_malloc(bufsz);
		rc = archive_read_data(arch, buf, (gsize)bufsz);
		if (rc < 0) {
			g_set_error(error,
				    G_IO_ERROR,
				    G_IO_ERROR_FAILED,
				    "cannot read data: %s",
				    archive_error_string(arch));
			return FALSE;
		}
		if (rc != bufsz) {
			g_set_error(error,
				    G_IO_ERROR,
				    G_IO_ERROR_FAILED,
				    "read %" G_GSSIZE_FORMAT " of %" G_GINT64_FORMAT,
				    rc,
				    bufsz);
			return FALSE;
		}
		if (flags & FU_ARCHIVE_FLAG_IGNORE_PATH) {
			fn_key = g_path_get_basename(fn);
		} else {
			fn_key = g_strdup(fn);
		}
		g_debug("adding %s [%" G_GINT64_FORMAT "]", fn_key, bufsz);
		bytes = g_bytes_new_take(g_steal_pointer(&buf), bufsz);
		fu_archive_add_entry(self, fn_key, bytes);
	}

	/* success */
	return TRUE;
#else
	g_set_error_literal(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_NOT_SUPPORTED,
			    "missing libarchive support");
	return FALSE;
#endif
}

/**
 * fu_archive_new:
 * @data: (nullable): archive contents
 * @flags: archive flags, e.g. %FU_ARCHIVE_FLAG_NONE
 * @error: (nullable): optional return location for an error
 *
 * Parses @data as an archive and decompresses all files to memory blobs.
 *
 * If @data is unspecified then a new empty archive is created.
 *
 * Returns: a #FuArchive, or %NULL if the archive was invalid in any way.
 *
 * Since: 1.2.2
 **/
FuArchive *
fu_archive_new(GBytes *data, FuArchiveFlags flags, GError **error)
{
	g_autoptr(FuArchive) self = g_object_new(FU_TYPE_ARCHIVE, NULL);
	g_return_val_if_fail(error == NULL || *error == NULL, NULL);
	if (data != NULL) {
		if (!fu_archive_load(self, data, flags, error))
			return NULL;
	}
	return g_steal_pointer(&self);
}

#ifdef HAVE_LIBARCHIVE
static gssize
fu_archive_write_cb(struct archive *arch, void *user_data, const void *buf, gsize bufsz)
{
	GByteArray *blob = (GByteArray *)user_data;
	g_byte_array_append(blob, buf, bufsz);
	return (gssize)bufsz;
}
#endif

/**
 * fu_archive_write:
 * @self: a #FuArchive
 * @format: a compression, e.g. `FU_ARCHIVE_FORMAT_ZIP`
 * @compression: a compression, e.g. `FU_ARCHIVE_COMPRESSION_NONE`
 * @error: (nullable): optional return location for an error
 *
 * Writes an archive with specified @format and @compression.
 *
 * Returns: (transfer full): the archive blob
 *
 * Since: 1.8.1
 **/
GBytes *
fu_archive_write(FuArchive *self,
		 FuArchiveFormat format,
		 FuArchiveCompression compression,
		 GError **error)
{
#ifdef HAVE_LIBARCHIVE
	int r;
	g_autoptr(_archive_write_ctx) arch = NULL;
	g_autoptr(GByteArray) blob = g_byte_array_new();
	g_autoptr(GList) keys = NULL;

	g_return_val_if_fail(FU_IS_ARCHIVE(self), NULL);
	g_return_val_if_fail(format != FU_ARCHIVE_FORMAT_UNKNOWN, NULL);
	g_return_val_if_fail(compression != FU_ARCHIVE_COMPRESSION_UNKNOWN, NULL);
	g_return_val_if_fail(error == NULL || *error == NULL, NULL);

	/* sanity check */
#ifndef HAVE_LIBARCHIVE_WRITE_ADD_COMPRESSION_ZSTD
	if (compression == FU_ARCHIVE_COMPRESSION_ZSTD) {
		g_set_error_literal(error,
				    G_IO_ERROR,
				    G_IO_ERROR_NOT_SUPPORTED,
				    "archive_write_add_filter_zstd() not supported");
		return NULL;
	}
#endif

	/* compress anything matching either glob */
	arch = archive_write_new();
	if (arch == NULL) {
		g_set_error_literal(error,
				    G_IO_ERROR,
				    G_IO_ERROR_NOT_SUPPORTED,
				    "libarchive startup failed");
		return NULL;
	}
	fu_archive_set_compression(arch, compression);
	fu_archive_set_format(arch, format);
	r = archive_write_open(arch, blob, NULL, fu_archive_write_cb, NULL);
	if (r != 0) {
		g_set_error(error,
			    G_IO_ERROR,
			    G_IO_ERROR_NOT_SUPPORTED,
			    "cannot open: %s",
			    archive_error_string(arch));
		return NULL;
	}

	keys = g_hash_table_get_keys(self->entries);
	for (GList *l = keys; l != NULL; l = l->next) {
		const gchar *fn = l->data;
		GBytes *bytes = g_hash_table_lookup(self->entries, fn);
		gssize rc;
		g_autoptr(_archive_entry_ctx) entry = NULL;

		entry = archive_entry_new();
		archive_entry_set_pathname(entry, fn);
		archive_entry_set_filetype(entry, AE_IFREG);
		archive_entry_set_perm(entry, 0644);
		archive_entry_set_size(entry, g_bytes_get_size(bytes));

		r = archive_write_header(arch, entry);
		if (r != 0) {
			g_set_error(error,
				    G_IO_ERROR,
				    G_IO_ERROR_NOT_SUPPORTED,
				    "cannot write header: %s",
				    archive_error_string(arch));
			return NULL;
		}
		rc = archive_write_data(arch,
					g_bytes_get_data(bytes, NULL),
					g_bytes_get_size(bytes));
		if (rc < 0) {
			g_set_error(error,
				    G_IO_ERROR,
				    G_IO_ERROR_FAILED,
				    "cannot write data: %s",
				    archive_error_string(arch));
			return NULL;
		}
	}

	r = archive_write_close(arch);
	if (r != 0) {
		g_set_error(error,
			    G_IO_ERROR,
			    G_IO_ERROR_NOT_SUPPORTED,
			    "cannot close: %s",
			    archive_error_string(arch));
		return NULL;
	}

	/* success */
	return g_byte_array_free_to_bytes(g_steal_pointer(&blob));
#else
	g_set_error_literal(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_NOT_SUPPORTED,
			    "missing libarchive support");
	return NULL;
#endif
}
