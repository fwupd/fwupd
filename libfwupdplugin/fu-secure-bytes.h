/*
 * Copyright 2026 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include <fwupd.h>

G_BEGIN_DECLS

/**
 * FuSecureBytes:
 *
 * A #GByteArray that has its contents cleared to zero before being deallocated,
 * for holding sensitive data such as keys, tokens or passwords.
 *
 * Since: 2.1.8
 **/
typedef struct FuSecureBytes FuSecureBytes;

FuSecureBytes *
fu_secure_bytes_new(guint8 *buf, gsize bufsz, GDestroyNotify destroy_fn);
void
fu_secure_bytes_free(FuSecureBytes *self) G_GNUC_NON_NULL(1);
G_DEFINE_AUTOPTR_CLEANUP_FUNC(FuSecureBytes, fu_secure_bytes_free)

gsize
fu_secure_bytes_get_size(FuSecureBytes *self) G_GNUC_NON_NULL(1);
const guint8 *
fu_secure_bytes_get_data(FuSecureBytes *self) G_GNUC_NON_NULL(1);

FuSecureBytes *
fu_secure_bytes_get_contents(const gchar *filename, GError **error) G_GNUC_WARN_UNUSED_RESULT
    G_GNUC_NON_NULL(1);
gboolean
fu_secure_bytes_set_contents(FuSecureBytes *self, const gchar *filename, gint mode, GError **error)
    G_GNUC_WARN_UNUSED_RESULT G_GNUC_NON_NULL(1, 2);

G_END_DECLS
