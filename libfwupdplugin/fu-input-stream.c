/*
 * Copyright 2023 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#define G_LOG_DOMAIN "FuInputStream"

#include "config.h"

#include "fu-chunk-array.h"
#include "fu-crc-private.h"
#include "fu-input-stream.h"
#include "fu-mem-private.h"
#include "fu-sum.h"

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
			    FWUPD_ERROR,
			    FWUPD_ERROR_READ,
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
	guint8 tmp[0x8000];
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

	/* do not rely on composite input stream doing the right thing */
	if (count == G_MAXSIZE) {
		gsize streamsz = 0;
		if (!fu_input_stream_size(stream, &streamsz, error))
			return NULL;
		if (offset > streamsz) {
			g_set_error(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INTERNAL,
				    "offset 0x%x is out of range of stream size 0x%x",
				    (guint)offset,
				    (guint)streamsz);
			return NULL;
		}
		count = streamsz - offset;
	}

	/* seek back to start */
	if (G_IS_SEEKABLE(stream) && g_seekable_can_seek(G_SEEKABLE(stream))) {
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

	/* no data was read */
	if (buf->len == 0) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INVALID_FILE,
				    "no data could be read");
		return NULL;
	}

	/* success */
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
	return g_byte_array_free_to_bytes(g_steal_pointer(&buf)); /* nocheck:blocked */
}

/**
 * fu_input_stream_size:
 * @stream: a #GInputStream
 * @val: (out): size in bytes
 * @error: (nullable): optional return location for an error
 *
 * Reads the total possible of the stream.
 *
 * If @stream is not seekable, %G_MAXSIZE is used as the size.
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

	/* streaming from unseekable stream */
	if (!G_IS_SEEKABLE(stream) || !g_seekable_can_seek(G_SEEKABLE(stream))) {
		if (val != NULL)
			*val = G_MAXSIZE;
		return TRUE;
	}

	if (!g_seekable_seek(G_SEEKABLE(stream), 0, G_SEEK_END, NULL, error)) {
		g_prefix_error(error, "seek to end: ");
		return FALSE;
	}
	if (val != NULL)
		*val = g_seekable_tell(G_SEEKABLE(stream));

	/* success */
	return TRUE;
}

static gboolean
fu_input_stream_compute_checksum_cb(const guint8 *buf,
				    gsize bufsz,
				    gpointer user_data,
				    GError **error)
{
	GChecksum *csum = (GChecksum *)user_data;
	g_checksum_update(csum, buf, bufsz);
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
	g_autoptr(GChecksum) csum = g_checksum_new(checksum_type);

	g_return_val_if_fail(G_IS_INPUT_STREAM(stream), NULL);
	g_return_val_if_fail(error == NULL || *error == NULL, NULL);

	if (!fu_input_stream_chunkify(stream, fu_input_stream_compute_checksum_cb, csum, error))
		return NULL;
	return g_strdup(g_checksum_get_string(csum));
}

static gboolean
fu_input_stream_compute_sum8_cb(const guint8 *buf, gsize bufsz, gpointer user_data, GError **error)
{
	guint8 *value = (guint8 *)user_data;
	*value += fu_sum8(buf, bufsz);
	return TRUE;
}

/**
 * fu_input_stream_compute_sum8:
 * @stream: a #GInputStream
 * @value: (out): value
 * @error: (nullable): optional return location for an error
 *
 * Returns the arithmetic sum of all bytes in the stream.
 *
 * Returns: %TRUE for success
 *
 * Since: 2.0.0
 **/
gboolean
fu_input_stream_compute_sum8(GInputStream *stream, guint8 *value, GError **error)
{
	g_return_val_if_fail(G_IS_INPUT_STREAM(stream), FALSE);
	g_return_val_if_fail(value != NULL, FALSE);
	g_return_val_if_fail(error == NULL || *error == NULL, FALSE);
	return fu_input_stream_chunkify(stream, fu_input_stream_compute_sum8_cb, value, error);
}

static gboolean
fu_input_stream_compute_sum16_cb(const guint8 *buf, gsize bufsz, gpointer user_data, GError **error)
{
	guint16 *value = (guint16 *)user_data;
	*value += fu_sum16(buf, bufsz);
	return TRUE;
}

/**
 * fu_input_stream_compute_sum16:
 * @stream: a #GInputStream
 * @value: (out): value
 * @error: (nullable): optional return location for an error
 *
 * Returns the arithmetic sum of all bytes in the stream.
 *
 * Returns: %TRUE for success
 *
 * Since: 2.0.0
 **/
gboolean
fu_input_stream_compute_sum16(GInputStream *stream, guint16 *value, GError **error)
{
	g_return_val_if_fail(G_IS_INPUT_STREAM(stream), FALSE);
	g_return_val_if_fail(value != NULL, FALSE);
	g_return_val_if_fail(error == NULL || *error == NULL, FALSE);
	return fu_input_stream_chunkify(stream, fu_input_stream_compute_sum16_cb, value, error);
}

typedef struct {
	FuCrcKind kind;
	guint32 crc;
} FuInputStreamComputeCrc32Helper;

static gboolean
fu_input_stream_compute_crc32_cb(const guint8 *buf, gsize bufsz, gpointer user_data, GError **error)
{
	FuInputStreamComputeCrc32Helper *helper = (FuInputStreamComputeCrc32Helper *)user_data;
	helper->crc = fu_crc32_step(helper->kind, buf, bufsz, helper->crc);
	return TRUE;
}

/**
 * fu_input_stream_compute_crc32:
 * @stream: a #GInputStream
 * @kind: a #FuCrcKind, typically %FU_CRC_KIND_B32_STANDARD
 * @crc: (inout): initial and final CRC value
 * @error: (nullable): optional return location for an error
 *
 * Returns the cyclic redundancy check value for the given memory buffer.
 *
 * NOTE: The initial @crc differs from fu_crc32_step() in that it is inverted (to make it
 * symmetrical, and chainable), so for most uses you want to use the value of 0x0, not 0xFFFFFFFF.
 *
 * Returns: %TRUE for success
 *
 * Since: 2.0.0
 **/
gboolean
fu_input_stream_compute_crc32(GInputStream *stream, FuCrcKind kind, guint32 *crc, GError **error)
{
	FuInputStreamComputeCrc32Helper helper = {.crc = *crc, .kind = kind};
	g_return_val_if_fail(G_IS_INPUT_STREAM(stream), FALSE);
	g_return_val_if_fail(crc != NULL, FALSE);
	g_return_val_if_fail(error == NULL || *error == NULL, FALSE);
	if (!fu_input_stream_chunkify(stream, fu_input_stream_compute_crc32_cb, &helper, error))
		return FALSE;
	*crc = fu_crc32_done(kind, helper.crc);
	return TRUE;
}

typedef struct {
	FuCrcKind kind;
	guint16 crc;
} FuInputStreamComputeCrc16Helper;

static gboolean
fu_input_stream_compute_crc16_cb(const guint8 *buf, gsize bufsz, gpointer user_data, GError **error)
{
	FuInputStreamComputeCrc16Helper *helper = (FuInputStreamComputeCrc16Helper *)user_data;
	helper->crc = fu_crc16_step(helper->kind, buf, bufsz, helper->crc);
	return TRUE;
}

/**
 * fu_input_stream_compute_crc16:
 * @stream: a #GInputStream
 * @kind: a #FuCrcKind, typically %FU_CRC_KIND_B16_XMODEM
 * @crc: (inout): initial and final CRC value
 * @error: (nullable): optional return location for an error
 *
 * Returns the cyclic redundancy check value for the given memory buffer.
 *
 * NOTE: The initial @crc differs from fu_crc16() in that it is inverted (to make it
 * symmetrical, and chainable), so for most uses you want to use the value of 0x0, not 0xFFFF.
 *
 * Returns: %TRUE for success
 *
 * Since: 2.0.0
 **/
gboolean
fu_input_stream_compute_crc16(GInputStream *stream, FuCrcKind kind, guint16 *crc, GError **error)
{
	FuInputStreamComputeCrc16Helper helper = {.crc = *crc, .kind = kind};
	g_return_val_if_fail(G_IS_INPUT_STREAM(stream), FALSE);
	g_return_val_if_fail(crc != NULL, FALSE);
	g_return_val_if_fail(error == NULL || *error == NULL, FALSE);
	if (!fu_input_stream_chunkify(stream, fu_input_stream_compute_crc16_cb, &helper, error))
		return FALSE;
	*crc = fu_crc16_done(kind, helper.crc);
	return TRUE;
}

/**
 * fu_input_stream_chunkify:
 * @stream: a #GInputStream
 * @func_cb: (scope async): function to call with chunks
 * @user_data: user data to pass to @func_cb
 * @error: (nullable): optional return location for an error
 *
 * Split the stream into blocks and calls a function on each chunk.
 *
 * Returns: %TRUE for success
 *
 * Since: 2.0.0
 **/
gboolean
fu_input_stream_chunkify(GInputStream *stream,
			 FuInputStreamChunkifyFunc func_cb,
			 gpointer user_data,
			 GError **error)
{
	g_autoptr(FuChunkArray) chunks = NULL;

	g_return_val_if_fail(G_IS_INPUT_STREAM(stream), FALSE);
	g_return_val_if_fail(func_cb != NULL, FALSE);
	g_return_val_if_fail(error == NULL || *error == NULL, FALSE);

	chunks = fu_chunk_array_new_from_stream(stream, 0x0, 0x8000, error);
	if (chunks == NULL)
		return FALSE;
	for (gsize i = 0; i < fu_chunk_array_length(chunks); i++) {
		g_autoptr(FuChunk) chk = NULL;
		chk = fu_chunk_array_index(chunks, i, error);
		if (chk == NULL)
			return FALSE;
		if (!func_cb(fu_chunk_get_data(chk), fu_chunk_get_data_sz(chk), user_data, error))
			return FALSE;
	}
	return TRUE;
}

/**
 * fu_input_stream_find:
 * @stream: a #GInputStream
 * @buf: input buffer to look for
 * @bufsz: size of @buf
 * @offset: (nullable): found offset
 * @error: (nullable): optional return location for an error
 *
 * Find a memory buffer within an input stream, without loading the entire stream into a buffer.
 *
 * Returns: %TRUE if @buf was found
 *
 * Since: 2.0.0
 **/
gboolean
fu_input_stream_find(GInputStream *stream,
		     const guint8 *buf,
		     gsize bufsz,
		     gsize *offset,
		     GError **error)
{
	g_autoptr(GByteArray) buf_acc = g_byte_array_new();
	const gsize blocksz = 0x10000;
	gsize offset_add = 0;
	gsize offset_cur = 0;

	g_return_val_if_fail(G_IS_INPUT_STREAM(stream), FALSE);
	g_return_val_if_fail(buf != NULL, FALSE);
	g_return_val_if_fail(bufsz != 0, FALSE);
	g_return_val_if_fail(bufsz < blocksz, FALSE);
	g_return_val_if_fail(error == NULL || *error == NULL, FALSE);

	while (offset_cur < bufsz) {
		g_autoptr(GByteArray) buf_tmp = NULL;
		g_autoptr(GError) error_local = NULL;

		/* read more data */
		buf_tmp =
		    fu_input_stream_read_byte_array(stream, offset_cur, blocksz, &error_local);
		if (buf_tmp == NULL) {
			if (g_error_matches(error_local, FWUPD_ERROR, FWUPD_ERROR_INVALID_FILE))
				break;
			g_propagate_error(error, g_steal_pointer(&error_local));
			return FALSE;
		}
		g_byte_array_append(buf_acc, buf_tmp->data, buf_tmp->len);

		/* we found something */
		if (fu_memmem_safe(buf_acc->data, buf_acc->len, buf, bufsz, offset, NULL)) {
			if (offset != NULL)
				*offset += offset_add;
			return TRUE;
		}

		/* truncate the buffer */
		if (buf_acc->len > bufsz) {
			offset_add += buf_acc->len - bufsz;
			g_byte_array_remove_range(buf_acc, 0, buf_acc->len - bufsz);
		}

		/* move the offset */
		offset_cur += buf_tmp->len;
	}
	g_set_error(error,
		    FWUPD_ERROR,
		    FWUPD_ERROR_NOT_FOUND,
		    "failed to find buffer of size 0x%x",
		    (guint)bufsz);
	return FALSE;
}
