/*
 * Copyright (C) 2023 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#define G_LOG_DOMAIN "FuInputStream"

#include "config.h"

#include "fu-input-stream.h"
#include "fu-mem-private.h"

/**
 * fu_input_stream_from_path:
 * @path: a filename
 * @error: (nullable): optional return location for an error
 *
 * Opens the file as n input stream.
 *
 * Returns: (transfer full): a #GInputStream, or %NULL on error
 *
 * Since: 2.0.0
 **/
GInputStream *
fu_input_stream_from_path(const gchar *path, GError **error)
{
	g_autoptr(GFile) file = NULL;
	g_autoptr(GFileInputStream) stream = NULL;

	g_return_val_if_fail(path != NULL, NULL);
	g_return_val_if_fail(error == NULL || *error == NULL, NULL);

	file = g_file_new_for_path(path);
	stream = g_file_read(file, NULL, error);
	if (stream == NULL)
		return NULL;
	return G_INPUT_STREAM(g_steal_pointer(&stream));
}

/**
 * fu_input_stream_read_safe:
 * @stream: a #GInputStream
 * @buf (not nullable): a buffer to read data into
 * @bufsz: size of @buf
 * @offset: offset in bytes into @buf to copy from
 * @seek_set: given offset to seek to
 * @count: the number of bytes that will be read from the stream
 * @error: (nullable): optional return location for an error
 *
 * Tries to read count bytes from the stream into the buffer starting at @buf.
 *
 * Returns: %TRUE for success
 *
 * Since: 2.0.0
 **/
gboolean
fu_input_stream_read_safe(GInputStream *stream,
			  guint8 *buf,
			  gsize bufsz,
			  gsize offset,
			  gsize seek_set,
			  gsize count,
			  GError **error)
{
	gssize rc;

	g_return_val_if_fail(G_IS_INPUT_STREAM(stream), FALSE);
	g_return_val_if_fail(buf != NULL, FALSE);
	g_return_val_if_fail(error == NULL || *error == NULL, FALSE);

	if (!fu_memchk_write(bufsz, offset, count, error))
		return FALSE;
	if (!g_seekable_seek(G_SEEKABLE(stream), seek_set, G_SEEK_SET, NULL, error)) {
		g_prefix_error(error, "seek to 0x%x: ", (guint)seek_set);
		return FALSE;
	}
	rc = g_input_stream_read(stream, buf + offset, count, NULL, error);
	if (rc == -1) {
		g_prefix_error(error, "failed read of 0x%x: ", (guint)count);
		return FALSE;
	}
	if ((gsize)rc != count) {
		g_set_error(error,
			    G_IO_ERROR,
			    G_IO_ERROR_PARTIAL_INPUT,
			    "requested 0x%x and got 0x%x",
			    (guint)count,
			    (guint)rc);
		return FALSE;
	}
	return TRUE;
}

/**
 * fu_input_stream_read_u8:
 * @stream: a #GInputStream
 * @offset: offset in bytes into @stream to copy from
 * @value: (out) (not nullable): the parsed value
 * @error: (nullable): optional return location for an error
 *
 * Read a value from a stream using a specified endian in a safe way.
 *
 * Returns: %TRUE if @value was set, %FALSE otherwise
 *
 * Since: 2.0.0
 **/
gboolean
fu_input_stream_read_u8(GInputStream *stream, gsize offset, guint8 *value, GError **error)
{
	guint8 buf = 0;
	g_return_val_if_fail(G_IS_INPUT_STREAM(stream), FALSE);
	g_return_val_if_fail(value != NULL, FALSE);
	g_return_val_if_fail(error == NULL || *error == NULL, FALSE);
	if (!fu_input_stream_read_safe(stream, &buf, sizeof(buf), 0x0, offset, sizeof(buf), error))
		return FALSE;
	if (value != NULL)
		*value = buf;
	return TRUE;
}

/**
 * fu_input_stream_read_u16:
 * @stream: a #GInputStream
 * @offset: offset in bytes into @stream to copy from
 * @value: (out) (not nullable): the parsed value
 * @endian: an endian type, e.g. %G_LITTLE_ENDIAN
 * @error: (nullable): optional return location for an error
 *
 * Read a value from a stream using a specified endian in a safe way.
 *
 * Returns: %TRUE if @value was set, %FALSE otherwise
 *
 * Since: 2.0.0
 **/
gboolean
fu_input_stream_read_u16(GInputStream *stream,
			 gsize offset,
			 guint16 *value,
			 FuEndianType endian,
			 GError **error)
{
	guint8 buf[2] = {0};
	g_return_val_if_fail(G_IS_INPUT_STREAM(stream), FALSE);
	g_return_val_if_fail(value != NULL, FALSE);
	g_return_val_if_fail(error == NULL || *error == NULL, FALSE);
	if (!fu_input_stream_read_safe(stream, buf, sizeof(buf), 0x0, offset, sizeof(buf), error))
		return FALSE;
	if (value != NULL)
		*value = fu_memread_uint16(buf, endian);
	return TRUE;
}

/**
 * fu_input_stream_read_u24:
 * @stream: a #GInputStream
 * @offset: offset in bytes into @stream to copy from
 * @value: (out) (not nullable): the parsed value
 * @endian: an endian type, e.g. %G_LITTLE_ENDIAN
 * @error: (nullable): optional return location for an error
 *
 * Read a value from a stream using a specified endian in a safe way.
 *
 * Returns: %TRUE if @value was set, %FALSE otherwise
 *
 * Since: 2.0.0
 **/
gboolean
fu_input_stream_read_u24(GInputStream *stream,
			 gsize offset,
			 guint32 *value,
			 FuEndianType endian,
			 GError **error)
{
	guint8 buf[3] = {0};
	g_return_val_if_fail(G_IS_INPUT_STREAM(stream), FALSE);
	g_return_val_if_fail(value != NULL, FALSE);
	g_return_val_if_fail(error == NULL || *error == NULL, FALSE);
	if (!fu_input_stream_read_safe(stream, buf, sizeof(buf), 0x0, offset, sizeof(buf), error))
		return FALSE;
	if (value != NULL)
		*value = fu_memread_uint24(buf, endian);
	return TRUE;
}

/**
 * fu_input_stream_read_u32:
 * @stream: a #GInputStream
 * @offset: offset in bytes into @stream to copy from
 * @value: (out) (not nullable): the parsed value
 * @endian: an endian type, e.g. %G_LITTLE_ENDIAN
 * @error: (nullable): optional return location for an error
 *
 * Read a value from a stream using a specified endian in a safe way.
 *
 * Returns: %TRUE if @value was set, %FALSE otherwise
 *
 * Since: 2.0.0
 **/
gboolean
fu_input_stream_read_u32(GInputStream *stream,
			 gsize offset,
			 guint32 *value,
			 FuEndianType endian,
			 GError **error)
{
	guint8 buf[4] = {0};
	g_return_val_if_fail(G_IS_INPUT_STREAM(stream), FALSE);
	g_return_val_if_fail(value != NULL, FALSE);
	g_return_val_if_fail(error == NULL || *error == NULL, FALSE);
	if (!fu_input_stream_read_safe(stream, buf, sizeof(buf), 0x0, offset, sizeof(buf), error))
		return FALSE;
	if (value != NULL)
		*value = fu_memread_uint32(buf, endian);
	return TRUE;
}

/**
 * fu_input_stream_read_u64:
 * @stream: a #GInputStream
 * @offset: offset in bytes into @stream to copy from
 * @value: (out) (not nullable): the parsed value
 * @endian: an endian type, e.g. %G_LITTLE_ENDIAN
 * @error: (nullable): optional return location for an error
 *
 * Read a value from a stream using a specified endian in a safe way.
 *
 * Returns: %TRUE if @value was set, %FALSE otherwise
 *
 * Since: 2.0.0
 **/
gboolean
fu_input_stream_read_u64(GInputStream *stream,
			 gsize offset,
			 guint64 *value,
			 FuEndianType endian,
			 GError **error)
{
	guint8 buf[8] = {0};
	g_return_val_if_fail(G_IS_INPUT_STREAM(stream), FALSE);
	g_return_val_if_fail(value != NULL, FALSE);
	g_return_val_if_fail(error == NULL || *error == NULL, FALSE);
	if (!fu_input_stream_read_safe(stream, buf, sizeof(buf), 0x0, offset, sizeof(buf), error))
		return FALSE;
	if (value != NULL)
		*value = fu_memread_uint64(buf, endian);
	return TRUE;
}

/**
 * fu_input_stream_read_byte_array:
 * @stream: a #GInputStream
 * @offset: offset in bytes into @stream to copy from
 * @count: maximum number of bytes to read
 * @error: (nullable): optional return location for an error
 *
 * Read a byte array from a stream in a safe way.
 *
 * NOTE: The returned buffer may be smaller than @count!
 *
 * Returns: (transfer full): buffer
 *
 * Since: 2.0.0
 **/
GByteArray *
fu_input_stream_read_byte_array(GInputStream *stream, gsize offset, gsize count, GError **error)
{
	guint8 tmp[0x8000] = {0x0};
	g_autoptr(GByteArray) buf = g_byte_array_new();
	g_autoptr(GError) error_local = NULL;

	g_return_val_if_fail(G_IS_INPUT_STREAM(stream), NULL);
	g_return_val_if_fail(error == NULL || *error == NULL, NULL);

	/* this is invalid */
	if (count == 0) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_NOT_SUPPORTED,
				    "A maximum read size must be specified");
		return NULL;
	}

	/* seek back to start */
	if (g_seekable_can_seek(G_SEEKABLE(stream))) {
		if (!g_seekable_seek(G_SEEKABLE(stream), offset, G_SEEK_SET, NULL, error))
			return NULL;
	}

	/* read from stream in 32kB chunks */
	while (TRUE) {
		gssize sz;
		sz = g_input_stream_read(stream,
					 tmp,
					 MIN(count - buf->len, sizeof(tmp)),
					 NULL,
					 &error_local);
		if (sz == 0)
			break;
		if (sz < 0) {
			g_set_error_literal(error,
					    FWUPD_ERROR,
					    FWUPD_ERROR_INVALID_FILE,
					    error_local->message);
			return NULL;
		}
		g_byte_array_append(buf, tmp, sz);
		if (buf->len >= count)
			break;
	}
	return g_steal_pointer(&buf);
}

/**
 * fu_input_stream_read_bytes:
 * @stream: a #GInputStream
 * @offset: offset in bytes into @stream to copy from
 * @count: maximum number of bytes to read
 * @error: (nullable): optional return location for an error
 *
 * Read a #GBytes from a stream in a safe way.
 *
 * NOTE: The returned buffer may be smaller than @count!
 *
 * Returns: (transfer full): buffer
 *
 * Since: 2.0.0
 **/
GBytes *
fu_input_stream_read_bytes(GInputStream *stream, gsize offset, gsize count, GError **error)
{
	g_autoptr(GByteArray) buf = NULL;
	g_return_val_if_fail(G_IS_INPUT_STREAM(stream), NULL);
	g_return_val_if_fail(error == NULL || *error == NULL, NULL);
	buf = fu_input_stream_read_byte_array(stream, offset, count, error);
	if (buf == NULL)
		return NULL;
	return g_byte_array_free_to_bytes(g_steal_pointer(&buf)); /* nocheck */
}

/**
 * fu_input_stream_size:
 * @stream: a #GInputStream
 * @val: (out): size in bytes
 * @error: (nullable): optional return location for an error
 *
 * Reads the total possible of the stream.
 *
 * Returns: %TRUE for success
 *
 * Since: 2.0.0
 **/
gboolean
fu_input_stream_size(GInputStream *stream, gsize *val, GError **error)
{
	g_return_val_if_fail(G_IS_INPUT_STREAM(stream), FALSE);
	g_return_val_if_fail(error == NULL || *error == NULL, FALSE);

	if (!g_seekable_seek(G_SEEKABLE(stream), 0, G_SEEK_END, NULL, error)) {
		g_prefix_error(error, "seek to end: ");
		return FALSE;
	}
	if (val != NULL)
		*val = g_seekable_tell(G_SEEKABLE(stream));

	/* success */
	return TRUE;
}

/**
 * fu_input_stream_compute_checksum:
 * @stream: a #GInputStream
 * @checksum_type: a #GChecksumType
 * @error: (nullable): optional return location for an error
 *
 * Generates the checksum of the entire stream.
 *
 * Returns: the hexadecimal representation of the checksum, or %NULL on error
 *
 * Since: 2.0.0
 **/
gchar *
fu_input_stream_compute_checksum(GInputStream *stream, GChecksumType checksum_type, GError **error)
{
	guint8 buf[0x8000] = {0x0};
	g_autoptr(GChecksum) csum = g_checksum_new(checksum_type);

	g_return_val_if_fail(G_IS_INPUT_STREAM(stream), NULL);
	g_return_val_if_fail(error == NULL || *error == NULL, NULL);

	/* read from stream in 32kB chunks */
	if (!g_seekable_seek(G_SEEKABLE(stream), 0x0, G_SEEK_SET, NULL, error)) {
		g_prefix_error(error, "seek to start: ");
		return NULL;
	}
	while (TRUE) {
		gssize sz;
		sz = g_input_stream_read(stream, buf, sizeof(buf), NULL, error);
		if (sz == 0)
			break;
		if (sz == -1)
			return NULL;
		g_checksum_update(csum, buf, sz);
	}
	return g_strdup(g_checksum_get_string(csum));
}
