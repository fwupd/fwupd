/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2009-2016 Richard Hughes <richard@hughsie.com>
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

#ifndef __DFU_PROGRESS_BAR_H
#define __DFU_PROGRESS_BAR_H

#include <glib-object.h>

G_BEGIN_DECLS

#define DFU_TYPE_PROGRESS_BAR (dfu_progress_bar_get_type ())

G_DECLARE_FINAL_TYPE (DfuProgressBar, dfu_progress_bar, DFU, PROGRESS_BAR, GObject)

DfuProgressBar	*dfu_progress_bar_new			(void);
void		 dfu_progress_bar_set_size		(DfuProgressBar	*progress_bar,
							 guint		 size);
void		 dfu_progress_bar_set_padding		(DfuProgressBar	*progress_bar,
							 guint		 padding);
void		 dfu_progress_bar_set_percentage	(DfuProgressBar	*progress_bar,
							 gint		 percentage);
void		 dfu_progress_bar_start			(DfuProgressBar	*progress_bar,
							 const gchar	*text);
void		 dfu_progress_bar_end			(DfuProgressBar	*progress_bar);

G_END_DECLS

#endif /* __DFU_PROGRESS_BAR_H */
