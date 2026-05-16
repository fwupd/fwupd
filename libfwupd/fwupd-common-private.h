/*
 * Copyright 2017 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include <gio/gio.h>

#ifdef HAVE_GIO_UNIX
#include <gio/gunixinputstream.h>
#include <gio/gunixoutputstream.h>
#endif

#include "fwupd-common.h"

G_BEGIN_DECLS

#ifdef HAVE_GIO_UNIX
GUnixInputStream *
fwupd_unix_input_stream_from_bytes(GBytes *bytes, GError **error) G_GNUC_WARN_UNUSED_RESULT
    G_GNUC_NON_NULL(1);
GUnixInputStream *
fwupd_unix_input_stream_from_fn(const gchar *fn, GError **error) G_GNUC_WARN_UNUSED_RESULT
    G_GNUC_NON_NULL(1);
GUnixOutputStream *
fwupd_unix_output_stream_from_fn(const gchar *fn, GError **error) G_GNUC_WARN_UNUSED_RESULT
    G_GNUC_NON_NULL(1);
#endif

G_END_DECLS
