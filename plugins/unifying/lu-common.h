/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2016-2017 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
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

gchar		*lu_format_version		(const gchar	*name,
						 guint8		 major,
						 guint8		 minor,
						 guint16	 build);

gint		 lu_nonblock_open		(const gchar	*filename,
						 GError		**error);
gboolean	 lu_nonblock_read		(gint		 fd,
						 guint8		*data,
						 gsize		 data_sz,
						 gsize		*data_len,
						 guint		 timeout,
						 GError		**error);
gboolean	 lu_nonblock_write		(gint		 fd,
						 const guint8	*data,
						 gsize		 data_sz,
						 GError		**error);

G_END_DECLS

#endif /* __LU_COMMON_H */
