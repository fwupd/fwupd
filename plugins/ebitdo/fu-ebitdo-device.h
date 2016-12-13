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

G_BEGIN_DECLS

#define FU_EBITDO_TYPE_DEVICE (fu_ebitdo_device_get_type ())
G_DECLARE_DERIVABLE_TYPE (FuEbitdoDevice, fu_ebitdo_device, FU_EBITDO, DEVICE, GObject)

struct _FuEbitdoDeviceClass
{
	GObjectClass		parent_class;
	/*< private >*/
	void (*_as_reserved1)	(void);
	void (*_as_reserved2)	(void);
	void (*_as_reserved3)	(void);
	void (*_as_reserved4)	(void);
	void (*_as_reserved5)	(void);
	void (*_as_reserved6)	(void);
	void (*_as_reserved7)	(void);
	void (*_as_reserved8)	(void);
};

/**
 * FuEbitdoDeviceKind:
 * @FU_EBITDO_DEVICE_KIND_UNKNOWN:	Type invalid or not known
 * @FU_EBITDO_DEVICE_KIND_BOOTLOADER:	Bootloader
 * @FU_EBITDO_DEVICE_KIND_FC30:		FC30
 * @FU_EBITDO_DEVICE_KIND_NES30:	NES30
 * @FU_EBITDO_DEVICE_KIND_SFC30:	SFC30
 * @FU_EBITDO_DEVICE_KIND_SNES30:	SNES30
 * @FU_EBITDO_DEVICE_KIND_FC30PRO:	FC30PRO
 * @FU_EBITDO_DEVICE_KIND_NES30PRO:	NES30PRO
 * @FU_EBITDO_DEVICE_KIND_FC30_ARCADE:	FC30 ARCADE
 *
 * The device type.
 **/
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

FuEbitdoDevice	*fu_ebitdo_device_new			(GUsbDevice *usb_device);

/* helpers */
FuEbitdoDeviceKind fu_ebitdo_device_kind_from_string	(const gchar	*kind);
const gchar	*fu_ebitdo_device_kind_to_string	(FuEbitdoDeviceKind kind);

/* getters */
FuEbitdoDeviceKind fu_ebitdo_device_get_kind		(FuEbitdoDevice	*device);
GUsbDevice	*fu_ebitdo_device_get_usb_device	(FuEbitdoDevice	*device);
const gchar	*fu_ebitdo_device_get_version		(FuEbitdoDevice	*device);
const guint32	*fu_ebitdo_device_get_serial		(FuEbitdoDevice	*device);
const gchar	*fu_ebitdo_device_get_guid		(FuEbitdoDevice	*device);

/* object methods */
gboolean	 fu_ebitdo_device_open			(FuEbitdoDevice	*device,
							 GError		**error);
gboolean	 fu_ebitdo_device_close			(FuEbitdoDevice	*device,
							 GError		**error);
gboolean	 fu_ebitdo_device_write_firmware	(FuEbitdoDevice	*device,
							 GBytes		*fw,
							 GFileProgressCallback progress_cb,
							 gpointer	 progress_data,
							 GError		**error);

G_END_DECLS

#endif /* __FU_EBITDO_DEVICE_H */
