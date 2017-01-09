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

#ifndef __FU_DEVICE_ALTOS_H
#define __FU_DEVICE_ALTOS_H

#include <glib-object.h>
#include <gusb.h>

#include "fu-plugin.h"

G_BEGIN_DECLS

#define FU_TYPE_DEVICE_ALTOS (fu_device_altos_get_type ())
G_DECLARE_DERIVABLE_TYPE (FuDeviceAltos, fu_device_altos, FU, DEVICE_ALTOS, FuDevice)

struct _FuDeviceAltosClass
{
	FuDeviceClass		parent_class;
};

typedef enum {
	FU_DEVICE_ALTOS_KIND_UNKNOWN,
	FU_DEVICE_ALTOS_KIND_BOOTLOADER,
	FU_DEVICE_ALTOS_KIND_CHAOSKEY,
	/*< private >*/
	FU_DEVICE_ALTOS_KIND_LAST
} FuDeviceAltosKind;

typedef enum {
	FU_DEVICE_ALTOS_WRITE_FIRMWARE_FLAG_NONE	= 0,
	FU_DEVICE_ALTOS_WRITE_FIRMWARE_FLAG_REBOOT	= 1 << 0,
	/*< private >*/
	FU_DEVICE_ALTOS_WRITE_FIRMWARE_FLAG_LAST
} FuDeviceAltosWriteFirmwareFlag;

FuDeviceAltos	*fu_device_altos_new			(GUsbDevice *usb_device);
FuDeviceAltosKind fu_device_altos_kind_from_string	(const gchar	*kind);
const gchar	*fu_device_altos_kind_to_string		(FuDeviceAltosKind kind);
FuDeviceAltosKind fu_device_altos_get_kind		(FuDeviceAltos	*device);
gboolean	 fu_device_altos_probe			(FuDeviceAltos	*device,
							 GError		**error);
gboolean	 fu_device_altos_write_firmware		(FuDeviceAltos	*device,
							 GBytes		*fw,
							 FuDeviceAltosWriteFirmwareFlag flags,
							 GFileProgressCallback progress_cb,
							 gpointer	 progress_data,
							 GError		**error);
GBytes		*fu_device_altos_read_firmware		(FuDeviceAltos	*device,
							 GFileProgressCallback progress_cb,
							 gpointer	 progress_data,
							 GError		**error);

G_END_DECLS

#endif /* __FU_DEVICE_ALTOS_H */
