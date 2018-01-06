/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2015-2018 Richard Hughes <richard@hughsie.com>
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

#ifndef __FU_HISTORY_H
#define __FU_HISTORY_H

#include <glib-object.h>

#include "fu-device.h"

G_BEGIN_DECLS

#define FU_TYPE_PENDING (fu_history_get_type ())
G_DECLARE_FINAL_TYPE (FuHistory, fu_history, FU, HISTORY, GObject)

FuHistory	*fu_history_new				(void);

gboolean	 fu_history_add_device			(FuHistory	*self,
							 FuDevice	*device,
							 FwupdRelease	*release,
							 GError		**error);
gboolean	 fu_history_set_update_state		(FuHistory	*self,
							 FuDevice	*device,
							 FwupdUpdateState update_state,
							 GError		**error);
gboolean	 fu_history_set_device_flags		(FuHistory	*self,
							 FuDevice	*device,
							 FwupdDeviceFlags device_flags,
							 GError		**error);
gboolean	 fu_history_set_error_msg		(FuHistory	*self,
							 FuDevice	*device,
							 const gchar	*error_msg,
							 GError		**error);
gboolean	 fu_history_remove_device		(FuHistory	*self,
							 FuDevice	*device,
							 GError		**error);
gboolean	 fu_history_remove_all			(FuHistory	*self,
							 GError		**error);
gboolean	 fu_history_remove_all_with_state	(FuHistory	*self,
							 FwupdUpdateState update_state,
							 GError		**error);
FuDevice	*fu_history_get_device			(FuHistory	*self,
							 const gchar	*device_id,
							 GError		**error);
GPtrArray	*fu_history_get_devices			(FuHistory	*self,
							 GError		**error);

G_END_DECLS

#endif /* __FU_HISTORY_H */

