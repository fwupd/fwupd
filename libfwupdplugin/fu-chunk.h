/*
 * Copyright (C) 2017 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#pragma once

#include <glib-object.h>

#define FU_TYPE_CHUNK (fu_chunk_get_type ())

G_DECLARE_FINAL_TYPE (FuChunk, fu_chunk, FU, CHUNK, GObject)

FuChunk		*fu_chunk_bytes_new			(GBytes		*bytes);
void		 fu_chunk_set_idx			(FuChunk	*self,
							 guint32	 idx);
guint32		 fu_chunk_get_idx			(FuChunk	*self);
void		 fu_chunk_set_page			(FuChunk	*self,
							 guint32	 page);
guint32		 fu_chunk_get_page			(FuChunk	*self);
void		 fu_chunk_set_address			(FuChunk	*self,
							 guint32	 address);
guint32		 fu_chunk_get_address			(FuChunk	*self);
const guint8	*fu_chunk_get_data			(FuChunk	*self);
guint8		*fu_chunk_get_data_out			(FuChunk	*self);
guint32		 fu_chunk_get_data_sz			(FuChunk	*self);
void		 fu_chunk_set_bytes			(FuChunk	*self,
							 GBytes		*bytes);
GBytes		*fu_chunk_get_bytes			(FuChunk	*self);

FuChunk		*fu_chunk_new				(guint32	 idx,
							 guint32	 page,
							 guint32	 address,
							 const guint8	*data,
							 guint32	 data_sz);
gchar		*fu_chunk_to_string			(FuChunk	*self);

gchar		*fu_chunk_array_to_string		(GPtrArray	*chunks);
GPtrArray	*fu_chunk_array_new			(const guint8	*data,
							 guint32	 data_sz,
							 guint32	 addr_start,
							 guint32	 page_sz,
							 guint32	 packet_sz);
GPtrArray	*fu_chunk_array_mutable_new		(guint8		*data,
							 guint32	 data_sz,
							 guint32	 addr_start,
							 guint32	 page_sz,
							 guint32	 packet_sz);
GPtrArray	*fu_chunk_array_new_from_bytes		(GBytes		*blob,
							 guint32	 addr_start,
							 guint32	 page_sz,
							 guint32	 packet_sz);
