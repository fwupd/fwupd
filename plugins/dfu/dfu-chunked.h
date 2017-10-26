/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2017 Richard Hughes <richard@hughsie.com>
 *
 * Licensed under the GNU Lesser General Public License Version 2.1
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301 USA
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

G_END_DECLS

#endif /* __DFU_CHUNKED_H */
