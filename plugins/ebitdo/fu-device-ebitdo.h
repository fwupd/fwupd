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

#ifndef __FU_DEVICE_EBITDO_H
#define __FU_DEVICE_EBITDO_H

#include <glib-object.h>
#include <gusb.h>

#include "fu-plugin.h"

G_BEGIN_DECLS

#define FU_TYPE_DEVICE_EBITDO (fu_device_ebitdo_get_type ())
G_DECLARE_DERIVABLE_TYPE (FuDeviceEbitdo, fu_device_ebitdo, FU, DEVICE_EBITDO, FuDevice)

struct _FuDeviceEbitdoClass
{
	FuDeviceClass		parent_class;
};

typedef enum {
	FU_DEVICE_EBITDO_KIND_UNKNOWN,
	FU_DEVICE_EBITDO_KIND_BOOTLOADER,
	FU_DEVICE_EBITDO_KIND_FC30,
	FU_DEVICE_EBITDO_KIND_NES30,
	FU_DEVICE_EBITDO_KIND_SFC30,
	FU_DEVICE_EBITDO_KIND_SNES30,
	FU_DEVICE_EBITDO_KIND_FC30PRO,
	FU_DEVICE_EBITDO_KIND_NES30PRO,
	FU_DEVICE_EBITDO_KIND_FC30_ARCADE,
	/*< private >*/
	FU_DEVICE_EBITDO_KIND_LAST
} FuDeviceEbitdoKind;

FuDeviceEbitdo	*fu_device_ebitdo_new			(GUsbDevice	*usb_device);
gboolean	 fu_device_ebitdo_set_usb_device	(FuDeviceEbitdo	*device,
							 GUsbDevice	*usb_device,
							 GError		**error);

/* helpers */
FuDeviceEbitdoKind fu_device_ebitdo_kind_from_string	(const gchar	*kind);
const gchar	*fu_device_ebitdo_kind_to_string	(FuDeviceEbitdoKind kind);

/* getters */
FuDeviceEbitdoKind fu_device_ebitdo_get_kind		(FuDeviceEbitdo	*device);
const guint32	*fu_device_ebitdo_get_serial		(FuDeviceEbitdo	*device);

/* object methods */
gboolean	 fu_device_ebitdo_open			(FuDeviceEbitdo	*device,
							 GError		**error);
gboolean	 fu_device_ebitdo_close			(FuDeviceEbitdo	*device,
							 GError		**error);
gboolean	 fu_device_ebitdo_write_firmware	(FuDeviceEbitdo	*device,
							 GBytes		*fw,
							 GFileProgressCallback progress_cb,
							 gpointer	 progress_data,
							 GError		**error);

G_END_DECLS

#endif /* __FU_DEVICE_EBITDO_H */
