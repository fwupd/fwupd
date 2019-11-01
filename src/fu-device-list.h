/*
 * Copyright (C) 2017 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#pragma once

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
FuDevice	*fu_device_list_get_by_id		(FuDeviceList	*self,
							 const gchar	*device_id,
							 GError		**error);
FuDevice	*fu_device_list_get_by_guid		(FuDeviceList	*self,
							 const gchar	*guid,
							 GError		**error);
gboolean	 fu_device_list_wait_for_replug		(FuDeviceList	*self,
							 FuDevice	*device,
							 GError		**error);
