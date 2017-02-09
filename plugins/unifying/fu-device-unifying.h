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

#ifndef __FU_DEVICE_UNIFYING_H
#define __FU_DEVICE_UNIFYING_H

#include <glib-object.h>
#include <gusb.h>

#include "fu-plugin.h"

G_BEGIN_DECLS

#define FU_TYPE_DEVICE_UNIFYING (fu_device_unifying_get_type ())
G_DECLARE_DERIVABLE_TYPE (FuDeviceUnifying, fu_device_unifying, FU, DEVICE_UNIFYING, FuDevice)

struct _FuDeviceUnifyingClass
{
	FuDeviceClass		parent_class;
};

typedef enum {
	FU_DEVICE_UNIFYING_KIND_UNKNOWN,
	FU_DEVICE_UNIFYING_KIND_RUNTIME,
	FU_DEVICE_UNIFYING_KIND_BOOTLOADER_NORDIC,
	FU_DEVICE_UNIFYING_KIND_BOOTLOADER_TEXAS,
	/*< private >*/
	FU_DEVICE_UNIFYING_KIND_LAST
} FuDeviceUnifyingKind;

FuDeviceUnifying	*fu_device_unifying_new		(GUsbDevice 		*usb_device);
FuDeviceUnifying	*fu_device_unifying_emulated_new (FuDeviceUnifyingKind	 kind);

FuDeviceUnifyingKind fu_device_unifying_kind_from_string (const gchar		*kind);
const gchar	*fu_device_unifying_kind_to_string	(FuDeviceUnifyingKind	 kind);

FuDeviceUnifyingKind fu_device_unifying_get_kind	(FuDeviceUnifying	*device);
GUsbDevice	*fu_device_unifying_get_usb_device	(FuDeviceUnifying	*device);

gboolean	 fu_device_unifying_open		(FuDeviceUnifying	*device,
							 GError			**error);
gboolean	 fu_device_unifying_detach		(FuDeviceUnifying	*device,
							 GError			**error);
gboolean	 fu_device_unifying_attach		(FuDeviceUnifying	*device,
							 GError			**error);
gboolean	 fu_device_unifying_close		(FuDeviceUnifying	*device,
							 GError			**error);
gboolean	 fu_device_unifying_write_firmware	(FuDeviceUnifying	*device,
							 GBytes			*fw,
							 GFileProgressCallback	 progress_cb,
							 gpointer		 progress_data,
							 GError			**error);

G_END_DECLS

#endif /* __FU_DEVICE_UNIFYING_H */
