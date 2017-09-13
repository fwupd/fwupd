/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2015 Richard Hughes <richard@hughsie.com>
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

#ifndef __FU_PENDING_H
#define __FU_PENDING_H

#include <glib-object.h>

#include "fu-device.h"

G_BEGIN_DECLS

#define FU_TYPE_PENDING (fu_pending_get_type ())
G_DECLARE_DERIVABLE_TYPE (FuPending, fu_pending, FU, PENDING, GObject)

struct _FuPendingClass
{
	GObjectClass		 parent_class;
};

FuPending	*fu_pending_new				(void);

gboolean	 fu_pending_add_device			(FuPending	*pending,
							 FuDevice	*device,
							 GError		**error);
gboolean	 fu_pending_set_state			(FuPending	*pending,
							 FuDevice	*device,
							 FwupdUpdateState state,
							 GError		**error);
gboolean	 fu_pending_set_error_msg		(FuPending	*pending,
							 FuDevice	*device,
							 const gchar	*error_msg,
							 GError		**error);
gboolean	 fu_pending_remove_device		(FuPending	*pending,
							 FuDevice	*device,
							 GError		**error);
gboolean	 fu_pending_remove_all			(FuPending	*pending,
							 GError		**error);
FuDevice	*fu_pending_get_device			(FuPending	*pending,
							 const gchar	*device_id,
							 GError		**error);
GPtrArray	*fu_pending_get_devices			(FuPending	*pending,
							 GError		**error);

G_END_DECLS

#endif /* __FU_PENDING_H */

