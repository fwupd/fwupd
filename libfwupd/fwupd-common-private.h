/*
 * Copyright (C) 2017 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#pragma once

#include <gio/gio.h>

#ifdef HAVE_GIO_UNIX
#include <gio/gunixinputstream.h>
#endif

#include "fwupd-common.h"

G_BEGIN_DECLS

GVariant	*fwupd_hash_kv_to_variant		(GHashTable	*hash);
GHashTable	*fwupd_variant_to_hash_kv		(GVariant	*dict);
gchar		*fwupd_build_user_agent_system		(void);

void		 fwupd_input_stream_read_bytes_async	(GInputStream	*stream,
							 GCancellable	*cancellable,
							 GAsyncReadyCallback callback,
							 gpointer	 callback_data);
GBytes		*fwupd_input_stream_read_bytes_finish	(GInputStream	*stream,
							 GAsyncResult	*res,
							 GError		**error)
							 G_GNUC_WARN_UNUSED_RESULT;

#ifdef HAVE_GIO_UNIX
GUnixInputStream *fwupd_unix_input_stream_from_bytes	(GBytes		*bytes,
							 GError		**error)
							 G_GNUC_WARN_UNUSED_RESULT;
GUnixInputStream *fwupd_unix_input_stream_from_fn	(const gchar	*fn,
							 GError		**error)
							 G_GNUC_WARN_UNUSED_RESULT;
#endif

G_END_DECLS
