/*
 * Copyright 2026 Red Hat
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#define G_LOG_DOMAIN "FuFileInputStream"

#include "config.h"

#include "fu-file-input-stream.h"
#include "fu-stream-input-stream-private.h"

/**
 * FuFileInputStream:
 *
 * An input stream that replaces #GFileInputStream, providing file-specific
 * operations within the #FuInputStream type hierarchy. This implementation
 * is a drop-in replacement for #GFileInputStream for the needs
 * within fwupd.
 */

struct _FuFileInputStream {
	FuStreamInputStream parent_instance;
	GFileInputStream *file_stream; /* nocheck:blocked */
};

G_DEFINE_TYPE(FuFileInputStream, fu_file_input_stream, FU_TYPE_STREAM_INPUT_STREAM)

static void
fu_file_input_stream_finalize(GObject *object)
{
	FuFileInputStream *self = FU_FILE_INPUT_STREAM(object);
	g_clear_object(&self->file_stream);
	G_OBJECT_CLASS(fu_file_input_stream_parent_class)->finalize(object);
}

static void
fu_file_input_stream_class_init(FuFileInputStreamClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS(klass);
	object_class->finalize = fu_file_input_stream_finalize;
}

static void
fu_file_input_stream_init(FuFileInputStream *self)
{
}

/**
 * fu_file_input_stream_from_file:
 * @file: a #GFile
 * @cancellable: (nullable): optional #GCancellable
 * @error: (nullable): optional return location for an error
 *
 * Opens a #GFile for reading and returns a #FuFileInputStream. Use
 * this instead of g_file_read().
 *
 * Returns: (transfer full): a #FuFileInputStream, or %NULL on error
 *
 * Since: 2.1.7
 **/
FuFileInputStream *
fu_file_input_stream_from_file(GFile *file, GCancellable *cancellable, GError **error)
{
	g_autoptr(GFileInputStream) file_stream = NULL; /* nocheck:blocked */
	g_autoptr(FuFileInputStream) self = NULL;

	g_return_val_if_fail(G_IS_FILE(file), NULL);
	g_return_val_if_fail(error == NULL || *error == NULL, NULL);

	file_stream = g_file_read(file, cancellable, error); /* nocheck:blocked */
	if (file_stream == NULL)
		return NULL;

	self = g_object_new(FU_TYPE_FILE_INPUT_STREAM, NULL);
	fu_stream_input_stream_set_base_stream(FU_STREAM_INPUT_STREAM(self),
					       G_INPUT_STREAM(file_stream));
	self->file_stream = g_object_ref(file_stream);

	return g_steal_pointer(&self);
}

/**
 * fu_file_input_stream_query_info:
 * @stream: a #FuFileInputStream
 * @attributes: a file attribute query string
 * @cancellable: (nullable): optional #GCancellable
 * @error: (nullable): optional return location for an error
 *
 * Queries a file input stream for the given @attributes.
 *
 * Returns: (transfer full): a #GFileInfo, or %NULL on error
 *
 * Since: 2.1.7
 **/
GFileInfo *
fu_file_input_stream_query_info(FuFileInputStream *stream,
				const gchar *attributes,
				GCancellable *cancellable,
				GError **error)
{
	g_return_val_if_fail(FU_IS_FILE_INPUT_STREAM(stream), NULL);
	g_return_val_if_fail(attributes != NULL, NULL);
	g_return_val_if_fail(error == NULL || *error == NULL, NULL);
	return g_file_input_stream_query_info(/* nocheck:blocked */
					      stream->file_stream,
					      attributes,
					      cancellable,
					      error);
}
