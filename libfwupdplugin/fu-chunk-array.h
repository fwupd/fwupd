/*
 * Copyright (C) 2023 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#pragma once

#include "fu-chunk.h"

#define FU_TYPE_CHUNK_ARRAY (fu_chunk_array_get_type())

G_DECLARE_FINAL_TYPE(FuChunkArray, fu_chunk_array, FU, CHUNK_ARRAY, GObject)

FuChunkArray *
fu_chunk_array_new_from_bytes(GBytes *blob, guint32 addr_start, guint32 packet_sz);
guint
fu_chunk_array_length(FuChunkArray *self);
FuChunk *
fu_chunk_array_index(FuChunkArray *self, guint idx);
