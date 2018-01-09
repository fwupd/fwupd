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

#ifndef __FU_DEVICE_LIST_H
#define __FU_DEVICE_LIST_H

G_BEGIN_DECLS

#include <glib-object.h>

#include "fu-device.h"

#define FU_TYPE_DEVICE_LIST (fu_device_list_get_type ())
G_DECLARE_FINAL_TYPE (FuDeviceList, fu_device_list, FU, DEVICE_LIST, GObject)

FuDeviceList	*fu_device_list_new			(void);
void		 fu_device_list_add			(FuDeviceList	*self,
							 FuDevice	*device);
void		 fu_device_list_remove			(FuDeviceList	*self,
							 FuDevice	*device);
GPtrArray	*fu_device_list_get_all			(FuDeviceList	*self);
GPtrArray	*fu_device_list_get_active		(FuDeviceList	*self);
FuDevice	*fu_device_list_get_old			(FuDeviceList	*self,
							 FuDevice	*device);
FuDevice	*fu_device_list_find_by_id		(FuDeviceList	*self,
							 const gchar	*device_id,
							 GError		**error);
FuDevice	*fu_device_list_find_by_guid		(FuDeviceList	*self,
							 const gchar	*guid,
							 GError		**error);

G_END_DECLS

#endif /* __FU_DEVICE_LIST_H */

