/*
 * Copyright 2023 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include <gio/gio.h>

#include "fwupd-build.h"

G_BEGIN_DECLS

#define FWUPD_TYPE_INPUT_STREAM (fwupd_input_stream_get_type())
G_DECLARE_DERIVABLE_TYPE(FwupdInputStream, fwupd_input_stream, FWUPD, INPUT_STREAM, GObject)

/**
 * FwupdStreamImpl:
 *
 * An opaque pointer to an underlying stream implementation.
 *
 * Since: 2.2.1
 */
typedef gpointer FwupdStreamImpl;

struct _FwupdInputStreamClass {
	GObjectClass parent_class;
	gssize (*read_fn)(FwupdInputStream *stream,
			  void *buffer,
			  gsize count,
			  GCancellable *cancellable,
			  GError **error);
	goffset (*tell)(FwupdInputStream *stream);
	gboolean (*can_seek)(FwupdInputStream *stream);
	gboolean (*seek)(FwupdInputStream *stream,
			 goffset offset,
			 GSeekType type,
			 GCancellable *cancellable,
			 GError **error);
	FwupdStreamImpl (*get_stream_impl)(FwupdInputStream *stream);
};

gssize
fwupd_input_stream_read(FwupdInputStream *stream,
			void *buffer,
			gsize count,
			GCancellable *cancellable,
			GError **error) G_GNUC_WARN_UNUSED_RESULT G_GNUC_NON_NULL(1, 2);
gboolean
fwupd_input_stream_read_all(FwupdInputStream *stream,
			    void *buffer,
			    gsize count,
			    gsize *bytes_read,
			    GCancellable *cancellable,
			    GError **error) G_GNUC_WARN_UNUSED_RESULT G_GNUC_NON_NULL(1, 2);

G_END_DECLS
