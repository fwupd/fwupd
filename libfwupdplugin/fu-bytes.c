/*
 * Copyright 2017 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#define G_LOG_DOMAIN "FuCommon"

#include "config.h"

#include "fwupd-error.h"

#include "fu-byte-array.h"
#include "fu-bytes.h"
#include "fu-mem.h"

/**
 * fu_bytes_set_contents:
 * @filename: a filename
 * @bytes: data to write
 * @error: (nullable): optional return location for an error
 *
 * Writes a blob of data to a filename, creating the parent directories as
 * required.
 *
 * Returns: %TRUE for success
 *
 * Since: 1.8.2
 **/
gboolean
fu_bytes_set_contents(const gchar *filename, GBytes *bytes, GError **error)
{
	const gchar *data;
	gsize size;
	g_autoptr(GFile) file = NULL;
	g_autoptr(GFile) file_parent = NULL;

	g_return_val_if_fail(filename != NULL, FALSE);
	g_return_val_if_fail(bytes != NULL, FALSE);
	g_return_val_if_fail(error == NULL || *error == NULL, FALSE);

	file = g_file_new_for_path(filename);
	file_parent = g_file_get_parent(file);
	if (!g_file_query_exists(file_parent, NULL)) {
		if (!g_file_make_directory_with_parents(file_parent, NULL, error))
			return FALSE;
	}
	data = g_bytes_get_data(bytes, &size);
	g_debug("writing %s with 0x%x bytes", filename, (guint)size);
	return g_file_set_contents(filename, data, size, error);
}

/**
 * fu_bytes_get_contents:
 * @filename: a filename
 * @error: (nullable): optional return location for an error
 *
 * Reads a blob of data from a file.
 *
 * Returns: a #GBytes, or %NULL for failure
 *
 * Since: 1.8.2
 **/
GBytes *
fu_bytes_get_contents(const gchar *filename, GError **error)
{
	g_autoptr(GError) error_local = NULL;
	g_autoptr(GMappedFile) mapped_file = NULL;

	g_return_val_if_fail(filename != NULL, NULL);
	g_return_val_if_fail(error == NULL || *error == NULL, NULL);

	/* try as a mapped file, falling back to reading it as a blob instead */
	mapped_file = g_mapped_file_new(filename, FALSE, &error_local);
	if (mapped_file == NULL || g_mapped_file_get_length(mapped_file) == 0) {
		gchar *data = NULL;
		gsize len = 0;
		if (!g_file_get_contents(filename, &data, &len, error)) {
			fwupd_error_convert(error);
			return NULL;
		}
		g_debug("failed to read as mapped file, "
			"so reading %s of size 0x%x: %s",
			filename,
			(guint)len,
			error_local != NULL ? error_local->message : "zero size");
		return g_bytes_new_take(data, len);
	}
	g_debug("mapped file %s of size 0x%x",
		filename,
		(guint)g_mapped_file_get_length(mapped_file));
	return g_mapped_file_get_bytes(mapped_file);
}

/**
 * fu_bytes_align:
 * @bytes: data blob
 * @blksz: block size in bytes
 * @padval: the byte used to pad the byte buffer
 *
 * Aligns a block of memory to @blksize using the @padval value; if
 * the block is already aligned then the original @bytes is returned.
 *
 * Returns: (transfer full): a #GBytes, possibly @bytes
 *
 * Since: 1.8.2
 **/
GBytes *
fu_bytes_align(GBytes *bytes, gsize blksz, gchar padval)
{
	const guint8 *data;
	gsize sz;

	g_return_val_if_fail(bytes != NULL, NULL);
	g_return_val_if_fail(blksz > 0, NULL);

	/* pad */
	data = g_bytes_get_data(bytes, &sz);
	if (sz % blksz != 0) {
		gsize sz_align = ((sz / blksz) + 1) * blksz;
		guint8 *data_align = g_malloc(sz_align);
		memcpy(data_align, data, sz); /* nocheck:blocked */
		memset(data_align + sz, padval, sz_align - sz);
		g_debug("aligning 0x%x bytes to 0x%x", (guint)sz, (guint)sz_align);
		return g_bytes_new_take(data_align, sz_align);
	}

	/* perfectly aligned */
	return g_bytes_ref(bytes);
}

/**
 * fu_bytes_is_empty:
 * @bytes: data blob
 *
 * Checks if a byte array are just empty (0xff) bytes.
 *
 * Returns: %TRUE if @bytes is empty
 *
 * Since: 1.8.2
 **/
gboolean
fu_bytes_is_empty(GBytes *bytes)
{
	gsize sz = 0;
	const guint8 *buf = g_bytes_get_data(bytes, &sz);
	for (gsize i = 0; i < sz; i++) {
		if (buf[i] != 0xff)
			return FALSE;
	}
	return TRUE;
}

/**
 * fu_bytes_compare:
 * @bytes1: a data blob
 * @bytes2: another #GBytes
 * @error: (nullable): optional return location for an error
 *
 * Compares the buffers for equality.
 *
 * Returns: %TRUE if @bytes1 and @bytes2 are identical
 *
 * Since: 1.8.2
 **/
gboolean
fu_bytes_compare(GBytes *bytes1, GBytes *bytes2, GError **error)
{
	const guint8 *buf1;
	const guint8 *buf2;
	gsize bufsz1;
	gsize bufsz2;

	g_return_val_if_fail(bytes1 != NULL, FALSE);
	g_return_val_if_fail(bytes2 != NULL, FALSE);
	g_return_val_if_fail(error == NULL || *error == NULL, FALSE);

	buf1 = g_bytes_get_data(bytes1, &bufsz1);
	buf2 = g_bytes_get_data(bytes2, &bufsz2);
	return fu_memcmp_safe(buf1, bufsz1, 0x0, buf2, bufsz2, 0x0, MAX(bufsz1, bufsz2), error);
}

/**
 * fu_bytes_pad:
 * @bytes: data blob
 * @sz: the desired size in bytes
 * @data: the byte used to pad the array
 *
 * Pads a GBytes to a minimum @sz with `0xff`.
 *
 * Returns: (transfer full): a data blob
 *
 * Since: 2.0.7
 **/
GBytes *
fu_bytes_pad(GBytes *bytes, gsize sz, guint8 data)
{
	gsize bytes_sz;

	g_return_val_if_fail(bytes != NULL, NULL);
	g_return_val_if_fail(sz != 0, NULL);

	/* pad */
	bytes_sz = g_bytes_get_size(bytes);
	if (bytes_sz < sz) {
		const guint8 *data_old = g_bytes_get_data(bytes, NULL);
		guint8 *data_new = g_malloc(sz);
		if (data_old != NULL)
			memcpy(data_new, data_old, bytes_sz); /* nocheck:blocked */
		memset(data_new + bytes_sz, data, sz - bytes_sz);
		return g_bytes_new_take(data_new, sz);
	}

	/* not required */
	return g_bytes_ref(bytes);
}

/**
 * fu_bytes_new_offset:
 * @bytes: data blob
 * @offset: where subsection starts at
 * @length: length of subsection
 * @error: (nullable): optional return location for an error
 *
 * Creates a #GBytes which is a subsection of another #GBytes.
 *
 * Returns: (transfer full): a #GBytes, or #NULL if range is invalid
 *
 * Since: 1.8.2
 **/
GBytes *
fu_bytes_new_offset(GBytes *bytes, gsize offset, gsize length, GError **error)
{
	g_return_val_if_fail(bytes != NULL, NULL);
	g_return_val_if_fail(error == NULL || *error == NULL, NULL);

	/* optimize */
	if (offset == 0x0 && g_bytes_get_size(bytes) == length)
		return g_bytes_ref(bytes);

	/* sanity check */
	if (offset + length < length || offset + length > g_bytes_get_size(bytes)) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INVALID_DATA,
			    "cannot create bytes @0x%02x for 0x%02x "
			    "as buffer only 0x%04x bytes in size",
			    (guint)offset,
			    (guint)length,
			    (guint)g_bytes_get_size(bytes));
		return NULL;
	}
	return g_bytes_new_from_bytes(bytes, offset, length);
}

/**
 * fu_bytes_get_data_safe:
 * @bytes: data blob
 * @bufsz: (out) (optional): location to return size of byte data
 * @error: (nullable): optional return location for an error
 *
 * Get the byte data in the #GBytes. This data should not be modified.
 * This function will always return the same pointer for a given #GBytes.
 *
 * If the size of @bytes is zero, then %NULL is returned and the @error is set,
 * which differs in behavior to that of g_bytes_get_data().
 *
 * This may be useful when calling g_mapped_file_new() on a zero-length file.
 *
 * Returns: a pointer to the byte data, or %NULL.
 *
 * Since: 1.6.0
 **/
const guint8 *
fu_bytes_get_data_safe(GBytes *bytes, gsize *bufsz, GError **error)
{
	const guint8 *buf = g_bytes_get_data(bytes, bufsz);
	if (buf == NULL) {
		g_set_error(error, FWUPD_ERROR, FWUPD_ERROR_INVALID_DATA, "invalid data");
		return NULL;
	}
	return buf;
}

/**
 * fu_bytes_to_string:
 * @bytes: data blob
 *
 * Converts @bytes to a lowercase hex string.
 *
 * Returns: (transfer full): a string, which may be zero length
 *
 * Since: 2.0.4
 **/
gchar *
fu_bytes_to_string(GBytes *bytes)
{
	const guint8 *buf;
	gsize bufsz = 0;
	g_autoptr(GString) str = g_string_new(NULL);

	g_return_val_if_fail(bytes != NULL, NULL);

	buf = g_bytes_get_data(bytes, &bufsz);
	for (gsize i = 0; i < bufsz; i++)
		g_string_append_printf(str, "%02x", buf[i]);
	return g_string_free(g_steal_pointer(&str), FALSE);
}

/**
 * fu_bytes_from_string:
 * @str: a hex string
 * @error: (nullable): optional return location for an error
 *
 * Converts a lowercase hex string to a #GBytes.
 *
 * Returns: (transfer full): a #GBytes, or %NULL on error
 *
 * Since: 2.0.5
 **/
GBytes *
fu_bytes_from_string(const gchar *str, GError **error)
{
	g_autoptr(GByteArray) buf = NULL;

	g_return_val_if_fail(str != NULL, NULL);
	g_return_val_if_fail(error == NULL || *error == NULL, NULL);

	buf = fu_byte_array_from_string(str, error);
	if (buf == NULL)
		return NULL;
	return g_byte_array_free_to_bytes(g_steal_pointer(&buf)); /* nocheck:blocked */
}
