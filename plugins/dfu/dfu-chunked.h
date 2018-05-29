/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2017 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#ifndef __DFU_CHUNKED_H
#define __DFU_CHUNKED_H

#include <glib.h>
#include <gusb.h>

G_BEGIN_DECLS

typedef struct {
	guint32		 idx;
	guint32		 page;
	guint32		 address;
	const guint8	*data;
	guint32		 data_sz;
} DfuChunkedPacket;

DfuChunkedPacket *dfu_chunked_packet_new		(guint32	 idx,
							 guint32	 page,
							 guint32	 address,
							 const guint8	*data,
							 guint32	 data_sz);
gchar		*dfu_chunked_packet_to_string		(DfuChunkedPacket	*item);

gchar		*dfu_chunked_to_string			(GPtrArray	*chunked);
GPtrArray	*dfu_chunked_new			(const guint8	*data,
							 guint32	 data_sz,
							 guint32	 addr_start,
							 guint32	 page_sz,
							 guint32	 packet_sz);
GPtrArray	*dfu_chunked_new_from_bytes		(GBytes		*blob,
							 guint32	 addr_start,
							 guint32	 page_sz,
							 guint32	 packet_sz);

G_END_DECLS

#endif /* __DFU_CHUNKED_H */
