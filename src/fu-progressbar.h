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

#ifndef __FU_PROGRESSBAR_H
#define __FU_PROGRESSBAR_H

#include <gio/gio.h>

#include "fwupd-enums.h"

G_BEGIN_DECLS

#define FU_TYPE_PROGRESSBAR (fu_progressbar_get_type ())
G_DECLARE_FINAL_TYPE (FuProgressbar, fu_progressbar, FU, PROGRESSBAR, GObject)

FuProgressbar	*fu_progressbar_new			(void);
void		 fu_progressbar_update			(FuProgressbar	*self,
							 FwupdStatus	 status,
							 guint		 percentage);
void		 fu_progressbar_set_length_status	(FuProgressbar	*self,
							 guint		 len);
void		 fu_progressbar_set_length_percentage	(FuProgressbar	*self,
							 guint		 len);

G_END_DECLS

#endif /* __FU_PROGRESSBAR_H */

