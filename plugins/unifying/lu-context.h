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

#ifndef __LU_CONTEXT_H
#define __LU_CONTEXT_H

#include <glib-object.h>
#include <gusb.h>

#include "lu-device.h"

G_BEGIN_DECLS

#define LU_TYPE_CONTEXT (lu_context_get_type ())
G_DECLARE_FINAL_TYPE (LuContext, lu_context, LU, CONTEXT, GObject)

GPtrArray	*lu_context_get_devices			(LuContext	*ctx);
LuDevice	*lu_context_find_by_platform_id		(LuContext	*ctx,
							 const gchar	*platform_id,
							 GError **error);
void		 lu_context_coldplug			(LuContext	*ctx);
gboolean	 lu_context_wait_for_replug		(LuContext	*ctx,
							 LuDevice	*device,
							 guint		 timeout_ms,
							 GError		**error);

LuContext	*lu_context_new				(GError		**error);
LuContext	*lu_context_new_full			(GUsbContext	*usb_ctx);

G_END_DECLS

#endif /* __LU_CONTEXT_H */
