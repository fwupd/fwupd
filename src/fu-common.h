/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2017 Richard Hughes <richard@hughsie.com>
 *
 * Licensed under the GNU General Public License Version 2
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#ifndef __FU_COMMON_H__
#define __FU_COMMON_H__

#include <gio/gio.h>

typedef enum {
	FU_APP_FLAGS_NONE		= 0,
	FU_APP_FLAGS_NO_IDLE_SOURCES	= 1 << 0,
	FU_APP_FLAGS_SEARCH_PWD		= 1 << 1,
	FU_APP_FLAGS_SEARCH_BUILDDIR	= 1 << 2,
	FU_APP_FLAGS_LAST
} FuAppFlags;

typedef void	(*FuOutputHandler)		(const gchar	*line,
						 gpointer	 user_data);

gboolean	 fu_common_spawn_sync		(const gchar * const *argv,
						 FuOutputHandler handler_cb,
						 gpointer	 handler_user_data,
						 GCancellable	*cancellable,
						 GError		**error);

gboolean	 fu_common_rmtree		(const gchar	*directory,
						 GError		**error);
GPtrArray	*fu_common_get_files_recursive	(const gchar	*path,
						 GError		**error);
gboolean	 fu_common_mkdir_parent		(const gchar	*filename,
						 GError		**error);
gboolean	 fu_common_set_contents_bytes	(const gchar	*filename,
						 GBytes		*bytes,
						 GError		**error);
GBytes		*fu_common_get_contents_bytes	(const gchar	*filename,
						 GError		**error);
GBytes		*fu_common_get_contents_fd	(gint		 fd,
						 gsize		 count,
						 GError		**error);
gboolean	 fu_common_extract_archive	(GBytes		*blob,
						 const gchar	*dir,
						 GError		**error);
GBytes		*fu_common_firmware_builder	(GBytes		*bytes,
						 const gchar	*script_fn,
						 const gchar	*output_fn,
						 GError		**error);

typedef guint FuEndianType;

void		 fu_common_write_uint16		(guint8		*buf,
						 guint16	 val_native,
						 FuEndianType	 endian);
void		 fu_common_write_uint32		(guint8		*buf,
						 guint32	 val_native,
						 FuEndianType	 endian);
guint16		 fu_common_read_uint16		(const guint8	*buf,
						 FuEndianType	 endian);
guint32		 fu_common_read_uint32		(const guint8	*buf,
						 FuEndianType	 endian);

#endif /* __FU_COMMON_H__ */
