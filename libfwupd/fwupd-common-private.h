/*
 * Copyright (C) 2017 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#pragma once

#include <glib.h>

#ifdef HAVE_GIO_UNIX
#include <gio/gunixinputstream.h>
#endif

#include "fwupd-common.h"

G_BEGIN_DECLS

gchar		*fwupd_checksum_format_for_display	(const gchar	*checksum);
GVariant	*fwupd_hash_kv_to_variant		(GHashTable	*hash);
GHashTable	*fwupd_variant_to_hash_kv		(GVariant	*dict);
gchar		*fwupd_build_user_agent_system		(void);

#ifdef HAVE_GIO_UNIX
GUnixInputStream *fwupd_unix_input_stream_from_bytes	(GBytes		*bytes,
							 GError		**error);
GUnixInputStream *fwupd_unix_input_stream_from_fn	(const gchar	*fn,
							 GError		**error);
#endif

G_END_DECLS
