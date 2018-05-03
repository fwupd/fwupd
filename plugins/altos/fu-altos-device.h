/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2016-2017 Richard Hughes <richard@hughsie.com>
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

#ifndef __FU_ALTOS_DEVICE_H
#define __FU_ALTOS_DEVICE_H

#include <glib-object.h>
#include <gusb.h>

#include "fu-plugin.h"

G_BEGIN_DECLS

#define FU_TYPE_ALTOS_DEVICE (fu_altos_device_get_type ())
G_DECLARE_DERIVABLE_TYPE (FuAltosDevice, fu_altos_device, FU, ALTOS_DEVICE, FuUsbDevice)

struct _FuAltosDeviceClass
{
	FuUsbDeviceClass	parent_class;
};

typedef enum {
	FU_ALTOS_DEVICE_KIND_UNKNOWN,
	FU_ALTOS_DEVICE_KIND_BOOTLOADER,
	FU_ALTOS_DEVICE_KIND_CHAOSKEY,
	/*< private >*/
	FU_ALTOS_DEVICE_KIND_LAST
} FuAltosDeviceKind;

typedef enum {
	FU_ALTOS_DEVICE_WRITE_FIRMWARE_FLAG_NONE	= 0,
	FU_ALTOS_DEVICE_WRITE_FIRMWARE_FLAG_REBOOT	= 1 << 0,
	/*< private >*/
	FU_ALTOS_DEVICE_WRITE_FIRMWARE_FLAG_LAST
} FuAltosDeviceWriteFirmwareFlag;

FuAltosDevice	*fu_altos_device_new			(GUsbDevice *usb_device);
FuAltosDeviceKind fu_altos_device_kind_from_string	(const gchar	*kind);
const gchar	*fu_altos_device_kind_to_string		(FuAltosDeviceKind kind);
FuAltosDeviceKind fu_altos_device_get_kind		(FuAltosDevice	*device);
gboolean	 fu_altos_device_probe			(FuAltosDevice	*device,
							 GError		**error);
GBytes		*fu_altos_device_read_firmware		(FuAltosDevice	*device,
							 GError		**error);

G_END_DECLS

#endif /* __FU_ALTOS_DEVICE_H */
