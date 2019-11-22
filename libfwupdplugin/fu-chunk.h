/*
 * Copyright (C) 2017-2018 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#pragma once

#include <glib.h>
#include <gusb.h>

typedef struct {
	guint32		 idx;
	guint32		 page;
	guint32		 address;
	const guint8	*data;
	guint32		 data_sz;
} FuChunk;

FuChunk		*fu_chunk_new				(guint32	 idx,
							 guint32	 page,
							 guint32	 address,
							 const guint8	*data,
							 guint32	 data_sz);
gchar		*fu_chunk_to_string			(FuChunk	*item);

gchar		*fu_chunk_array_to_string		(GPtrArray	*chunks);
GPtrArray	*fu_chunk_array_new			(const guint8	*data,
							 guint32	 data_sz,
							 guint32	 addr_start,
							 guint32	 page_sz,
							 guint32	 packet_sz);
GPtrArray	*fu_chunk_array_new_from_bytes		(GBytes		*blob,
							 guint32	 addr_start,
							 guint32	 page_sz,
							 guint32	 packet_sz);
