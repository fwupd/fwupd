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

#ifndef __LU_DEVICE_PERIPHERAL_H
#define __LU_DEVICE_PERIPHERAL_H

#include "lu-device.h"

G_BEGIN_DECLS

#define LU_TYPE_DEVICE_PERIPHERAL (lu_device_peripheral_get_type ())
G_DECLARE_FINAL_TYPE (LuDevicePeripheral, lu_device_peripheral, LU, DEVICE_PERIPHERAL, LuDevice)

typedef enum {
	LU_DEVICE_PERIPHERAL_KIND_KEYBOARD,
	LU_DEVICE_PERIPHERAL_KIND_REMOTE_CONTROL,
	LU_DEVICE_PERIPHERAL_KIND_NUMPAD,
	LU_DEVICE_PERIPHERAL_KIND_MOUSE,
	LU_DEVICE_PERIPHERAL_KIND_TOUCHPAD,
	LU_DEVICE_PERIPHERAL_KIND_TRACKBALL,
	LU_DEVICE_PERIPHERAL_KIND_PRESENTER,
	LU_DEVICE_PERIPHERAL_KIND_RECEIVER,
	LU_DEVICE_PERIPHERAL_KIND_LAST
} LuDevicePeripheralKind;

LuDevicePeripheralKind	 lu_device_peripheral_get_kind		(LuDevicePeripheral *self);
const gchar		*lu_device_peripheral_get_icon		(LuDevicePeripheral *self);
const gchar		*lu_device_peripheral_get_summary	(LuDevicePeripheral *self);

G_END_DECLS

#endif /* __LU_DEVICE_PERIPHERAL_H */
