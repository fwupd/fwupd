/*
 * Copyright 2017 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include "fwupd-build.h"

G_BEGIN_DECLS

GVariant *
fwupd_variant_from_hash_kv(GHashTable *hash) G_GNUC_NON_NULL(1);
GHashTable *
fwupd_variant_to_hash_kv(GVariant *dict) G_GNUC_NON_NULL(1);
guint32
fwupd_variant_get_uint32(GVariant *value) G_GNUC_NON_NULL(1);
guint64
fwupd_variant_get_uint64(GVariant *value) G_GNUC_NON_NULL(1);

G_END_DECLS
