/*
 * Copyright (C) 2017 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#pragma once

#include <glib.h>

#include "fwupd-common.h"

G_BEGIN_DECLS

gchar		*fwupd_checksum_format_for_display	(const gchar	*checksum);
GVariant	*fwupd_hash_kv_to_variant		(GHashTable	*hash);
GHashTable	*fwupd_variant_to_hash_kv		(GVariant	*dict);

G_END_DECLS
