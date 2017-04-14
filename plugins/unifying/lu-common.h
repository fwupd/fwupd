/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2016-2017 Richard Hughes <richard@hughsie.com>
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

#ifndef __LU_COMMON_H
#define __LU_COMMON_H

#include <glib.h>

G_BEGIN_DECLS

void		 lu_dump_raw			(const gchar	*title,
						 const guint8	*data,
						 gsize		 len);

guint8		 lu_buffer_read_uint8		(const gchar	*str);
guint16		 lu_buffer_read_uint16		(const gchar	*str);

gchar		*lu_format_version		(guint8		 major,
						 guint8		 minor,
						 guint16	 micro);

G_END_DECLS

#endif /* __LU_COMMON_H */
