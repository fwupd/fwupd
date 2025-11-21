/*
 * Copyright 2018 Richard Hughes <richard@hughsie.com>
 * Copyright 2022 Gaël PORTAY <gael.portay@collabora.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
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
#include "fu-bytes.h"
#include "fu-input-stream.h"

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
 * Returns: (transfer full): a #GBytes, or %NULL if the filename was not found
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
		g_set_error(error, FWUPD_ERROR, FWUPD_ERROR_NOT_FOUND, "no blob for %s", fn);
		return NULL;
	}
	return g_bytes_ref(bytes);
}

/**
 * fu_archive_iterate:
 * @self: a #FuArchive
 * @callback: (scope call) (closure user_data): a #FuArchiveIterateFunc.
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
fu_archive_read_ctx_free(_archive_read_ctx *arch)
{
	archive_read_close(arch);
	archive_read_free(arch);
}

G_DEFINE_AUTOPTR_CLEANUP_FUNC(_archive_read_ctx, fu_archive_read_ctx_free)

typedef struct archive _archive_write_ctx;

static void
fu_archive_write_ctx_free(_archive_write_ctx *arch)
{
	archive_write_close(arch);
	archive_write_free(arch);
}

G_DEFINE_AUTOPTR_CLEANUP_FUNC(_archive_write_ctx, fu_archive_write_ctx_free)

typedef struct archive_entry _archive_entry_ctx;

static void
fu_archive_entry_ctx_free(_archive_entry_ctx *entry)
{
	archive_entry_free(entry);
}

G_DEFINE_AUTOPTR_CLEANUP_FUNC(_archive_entry_ctx, fu_archive_entry_ctx_free)

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

static _archive_read_ctx *
fu_archive_read_new(GError **error)
{
	g_autoptr(_archive_read_ctx) arch = NULL;

	arch = archive_read_new();
	if (arch == NULL) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_NOT_SUPPORTED,
				    "libarchive startup failed");
		return NULL;
	}
	archive_read_support_format_all(arch);
	archive_read_support_filter_all(arch);
	return g_steal_pointer(&arch);
}

static gboolean
fu_archive_read(FuArchive *self, _archive_read_ctx *arch, FuArchiveFlags flags, GError **error)
{
	int r;
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
				    FWUPD_ERROR,
				    FWUPD_ERROR_INVALID_DATA,
				    "cannot read header: %s",
				    archive_error_string(arch));
			return FALSE;
		}

		/* only extract if valid */
		fn = archive_entry_pathname(entry);
		if (fn == NULL)
			continue;
		if (!archive_entry_size_is_set(entry)) {
			g_set_error(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INVALID_DATA,
				    "%s entry does not have size set",
				    fn);
			return FALSE;
		}
		bufsz = archive_entry_size(entry);
		if (bufsz > 1024 * 1024 * 1024) {
			g_set_error_literal(error,
					    FWUPD_ERROR,
					    FWUPD_ERROR_NOT_SUPPORTED,
					    "cannot read huge files");
			return FALSE;
		}
		buf = g_malloc(bufsz);
		rc = archive_read_data(arch, buf, (gsize)bufsz);
		if (rc < 0) {
			g_set_error(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_READ,
				    "cannot read data: %s",
				    archive_error_string(arch));
			return FALSE;
		}
		if (rc != bufsz) {
			g_set_error(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_READ,
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
}
#endif

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
#ifdef HAVE_LIBARCHIVE
	g_autoptr(FuArchive) self = g_object_new(FU_TYPE_ARCHIVE, NULL);

	g_return_val_if_fail(error == NULL || *error == NULL, NULL);

	if (data != NULL) {
		int r;
		g_autoptr(_archive_read_ctx) arch = NULL;

		arch = fu_archive_read_new(error);
		if (arch == NULL)
			return NULL;
		r = archive_read_open_memory(arch,
					     (void *)g_bytes_get_data(data, NULL),
					     (size_t)g_bytes_get_size(data));
		if (r != 0) {
			g_set_error(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_NOT_SUPPORTED,
				    "cannot open: %s",
				    archive_error_string(arch));
			return NULL;
		}
		if (!fu_archive_read(self, arch, flags, error))
			return NULL;
	}
	return g_steal_pointer(&self);
#else
	g_set_error_literal(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_NOT_SUPPORTED,
			    "missing libarchive support");
	return NULL;
#endif
}

#ifdef HAVE_LIBARCHIVE
typedef struct {
	GInputStream *stream;
	guint8 buf[0x8000];
} FuArchiveStreamHelper;

static gint64
fu_archive_skip_cb(struct archive *arch, void *client_data, off_t request)
{
	FuArchiveStreamHelper *helper = (FuArchiveStreamHelper *)client_data;
	gssize cnt;
	g_autoptr(GError) error_local = NULL;

	cnt = g_input_stream_skip(helper->stream, request, NULL, &error_local);
	if (cnt < 0) {
		archive_set_error(arch,
				  ARCHIVE_FAILED,
				  "failed to read from stream: %s",
				  error_local->message);
		return -1;
	}
	return cnt;
}

static gssize
fu_archive_read_cb(struct archive *arch, void *client_data, const void **buffer)
{
	FuArchiveStreamHelper *helper = (FuArchiveStreamHelper *)client_data;
	gssize cnt;
	g_autoptr(GError) error_local = NULL;

	cnt = g_input_stream_read(helper->stream,
				  helper->buf,
				  sizeof(helper->buf),
				  NULL,
				  &error_local);
	if (cnt < 0) {
		archive_set_error(arch,
				  ARCHIVE_FAILED,
				  "failed to read from stream: %s",
				  error_local->message);
		return -1;
	}
	if (cnt > 0)
		*buffer = helper->buf;
	return cnt;
}

static GSeekType
fu_archive_whence_to_seek_type(gint whence)
{
	if (whence == SEEK_SET)
		return G_SEEK_SET;
	if (whence == SEEK_END)
		return G_SEEK_END;
	return G_SEEK_CUR;
}

static gint64
fu_archive_seek_cb(struct archive *arch, void *client_data, gint64 offset, gint whence)
{
	FuArchiveStreamHelper *helper = (FuArchiveStreamHelper *)client_data;
	g_autoptr(GError) error_local = NULL;
	if (!g_seekable_seek(G_SEEKABLE(helper->stream),
			     offset,
			     fu_archive_whence_to_seek_type(whence),
			     NULL,
			     &error_local)) {
		archive_set_error(arch,
				  ARCHIVE_FAILED,
				  "failed to read from stream: %s",
				  error_local->message);
		return -1;
	}
	return g_seekable_tell(G_SEEKABLE(helper->stream));
}
#endif

/**
 * fu_archive_new_stream:
 * @stream: a #GInputStream
 * @flags: archive flags, e.g. %FU_ARCHIVE_FLAG_NONE
 * @error: (nullable): optional return location for an error
 *
 * Parses @stream as an archive and decompresses all files to memory blobs.
 *
 * Returns: a #FuArchive, or %NULL if the archive was invalid in any way.
 *
 * Since: 2.0.0
 **/
FuArchive *
fu_archive_new_stream(GInputStream *stream, FuArchiveFlags flags, GError **error)
{
#ifdef HAVE_LIBARCHIVE
	g_autoptr(FuArchive) self = g_object_new(FU_TYPE_ARCHIVE, NULL);
	g_autoptr(_archive_read_ctx) arch = NULL;
	FuArchiveStreamHelper helper = {.stream = stream};
	int r;

	g_return_val_if_fail(G_INPUT_STREAM(stream), NULL);
	g_return_val_if_fail(error == NULL || *error == NULL, NULL);

	if (!g_seekable_seek(G_SEEKABLE(stream), 0x0, G_SEEK_SET, NULL, error))
		return NULL;

	arch = fu_archive_read_new(error);
	if (arch == NULL)
		return NULL;
	archive_read_set_seek_callback(arch, fu_archive_seek_cb);
	archive_read_set_read_callback(arch, fu_archive_read_cb);
	archive_read_set_skip_callback(arch, fu_archive_skip_cb);
	archive_read_set_callback_data(arch, &helper);
	r = archive_read_open1(arch);
	if (r != 0) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_NOT_SUPPORTED,
			    "cannot open: %s",
			    archive_error_string(arch));
		return NULL;
	}
	if (!fu_archive_read(self, arch, flags, error))
		return NULL;
	return g_steal_pointer(&self);
#else
	g_set_error_literal(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_NOT_SUPPORTED,
			    "missing libarchive support");
	return NULL;
#endif
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
GByteArray *
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
				    FWUPD_ERROR,
				    FWUPD_ERROR_NOT_SUPPORTED,
				    "archive_write_add_filter_zstd() not supported");
		return NULL;
	}
#endif

	/* compress anything matching either glob */
	arch = archive_write_new();
	if (arch == NULL) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_NOT_SUPPORTED,
				    "libarchive startup failed");
		return NULL;
	}
	fu_archive_set_format(arch, format);
	if (format == FU_ARCHIVE_FORMAT_ZIP) {
		if (compression != FU_ARCHIVE_COMPRESSION_NONE)
			archive_write_set_options(arch, "zip:compression=deflate");
	} else {
		fu_archive_set_compression(arch, compression);
	}
	r = archive_write_open(arch, blob, NULL, fu_archive_write_cb, NULL);
	if (r != 0) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_NOT_SUPPORTED,
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
				    FWUPD_ERROR,
				    FWUPD_ERROR_NOT_SUPPORTED,
				    "cannot write header: %s",
				    archive_error_string(arch));
			return NULL;
		}
		rc = archive_write_data(arch,
					g_bytes_get_data(bytes, NULL),
					g_bytes_get_size(bytes));
		if (rc < 0) {
			g_set_error(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_WRITE,
				    "cannot write data: %s",
				    archive_error_string(arch));
			return NULL;
		}
	}

	r = archive_write_close(arch);
	if (r != 0) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_NOT_SUPPORTED,
			    "cannot close: %s",
			    archive_error_string(arch));
		return NULL;
	}

	/* success */
	return g_steal_pointer(&blob);
#else
	g_set_error_literal(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_NOT_SUPPORTED,
			    "missing libarchive support");
	return NULL;
#endif
}
