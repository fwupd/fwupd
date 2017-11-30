/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2016 Richard Hughes <richard@hughsie.com>
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

#ifndef __FU_EBITDO_DEVICE_H
#define __FU_EBITDO_DEVICE_H

#include <glib-object.h>
#include <gusb.h>

#include "fu-plugin.h"

G_BEGIN_DECLS

#define FU_TYPE_EBITDO_DEVICE (fu_ebitdo_device_get_type ())
G_DECLARE_DERIVABLE_TYPE (FuEbitdoDevice, fu_ebitdo_device, FU, EBITDO_DEVICE, FuUsbDevice)

struct _FuEbitdoDeviceClass
{
	FuUsbDeviceClass	parent_class;
};

typedef enum {
	FU_EBITDO_DEVICE_KIND_UNKNOWN,
	FU_EBITDO_DEVICE_KIND_BOOTLOADER,
	FU_EBITDO_DEVICE_KIND_FC30,
	FU_EBITDO_DEVICE_KIND_NES30,
	FU_EBITDO_DEVICE_KIND_SFC30,
	FU_EBITDO_DEVICE_KIND_SNES30,
	FU_EBITDO_DEVICE_KIND_FC30PRO,
	FU_EBITDO_DEVICE_KIND_NES30PRO,
	FU_EBITDO_DEVICE_KIND_FC30_ARCADE,
	/*< private >*/
	FU_EBITDO_DEVICE_KIND_LAST
} FuEbitdoDeviceKind;

FuEbitdoDevice	*fu_ebitdo_device_new			(GUsbDevice	*usb_device);

/* helpers */
FuEbitdoDeviceKind fu_ebitdo_device_kind_from_string	(const gchar	*kind);
const gchar	*fu_ebitdo_device_kind_to_string	(FuEbitdoDeviceKind kind);

/* getters */
FuEbitdoDeviceKind fu_ebitdo_device_get_kind		(FuEbitdoDevice	*device);
const guint32	*fu_ebitdo_device_get_serial		(FuEbitdoDevice	*device);

/* object methods */
gboolean	 fu_ebitdo_device_write_firmware	(FuEbitdoDevice	*device,
							 GBytes		*fw,
							 GError		**error);

G_END_DECLS

#endif /* __FU_EBITDO_DEVICE_H */
