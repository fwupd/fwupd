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

typedef void	(*FuOutputHandler)		(const gchar	*line,
						 gpointer	 user_data);

gboolean	 fu_common_spawn_sync		(const gchar * const *argv,
						 FuOutputHandler handler_cb,
						 gpointer	 handler_user_data,
						 GCancellable	*cancellable,
						 GError		**error);

gboolean	 fu_common_rmtree		(const gchar	*directory,
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

#endif /* __FU_COMMON_H__ */
