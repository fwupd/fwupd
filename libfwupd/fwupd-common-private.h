/*
 * Copyright 2017 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include <gio/gio.h>

#ifdef HAVE_GIO_UNIX
#include <gio/gunixoutputstream.h>
#endif

#include "fwupd-common.h"
#include "fwupd-input-stream-private.h"
#include "fwupd-unix-input-stream.h"

G_BEGIN_DECLS

FwupdUnixInputStream *
fwupd_unix_input_stream_from_bytes(GBytes *bytes, GError **error) G_GNUC_WARN_UNUSED_RESULT
    G_GNUC_NON_NULL(1);
FwupdUnixInputStream *
fwupd_unix_input_stream_from_stream(FwupdInputStream *stream,
				    GError **error) G_GNUC_WARN_UNUSED_RESULT G_GNUC_NON_NULL(1);

#ifdef HAVE_GIO_UNIX
GUnixOutputStream *
fwupd_unix_output_stream_from_fn(const gchar *fn, GError **error) G_GNUC_WARN_UNUSED_RESULT
    G_GNUC_NON_NULL(1);
#endif

FwupdInputStream *
fwupd_input_stream_from_fn(const gchar *filename, GError **error) G_GNUC_WARN_UNUSED_RESULT
    G_GNUC_NON_NULL(1);
FwupdInputStream *
fwupd_input_stream_from_bytes(GBytes *bytes, GError **error) G_GNUC_WARN_UNUSED_RESULT
    G_GNUC_NON_NULL(1);

G_END_DECLS
