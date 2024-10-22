/*
 * Copyright 2023 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include <fwupd.h>

#include "fu-chunk.h"

#define FU_TYPE_CHUNK_ARRAY (fu_chunk_array_get_type())

G_DECLARE_FINAL_TYPE(FuChunkArray, fu_chunk_array, FU, CHUNK_ARRAY, GObject)

FuChunkArray *
fu_chunk_array_new_virtual(gsize bufsz, gsize addr_offset, gsize page_sz, gsize packet_sz);
FuChunkArray *
fu_chunk_array_new_from_bytes(GBytes *blob, gsize addr_offset, gsize page_sz, gsize packet_sz)
    G_GNUC_NON_NULL(1);
FuChunkArray *
fu_chunk_array_new_from_stream(GInputStream *stream,
			       gsize addr_offset,
			       gsize page_sz,
			       gsize packet_sz,
			       GError **error) G_GNUC_NON_NULL(1);
guint
fu_chunk_array_length(FuChunkArray *self) G_GNUC_NON_NULL(1);
FuChunk *
fu_chunk_array_index(FuChunkArray *self, guint idx, GError **error) G_GNUC_NON_NULL(1);
