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

#ifndef __EBITDO_DEVICE_H
#define __EBITDO_DEVICE_H

#include <glib-object.h>
#include <gusb.h>

G_BEGIN_DECLS

#define EBITDO_TYPE_DEVICE (ebitdo_device_get_type ())
G_DECLARE_DERIVABLE_TYPE (EbitdoDevice, ebitdo_device, EBITDO, DEVICE, GObject)

struct _EbitdoDeviceClass
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
 * EbitdoDeviceKind:
 * @EBITDO_DEVICE_KIND_UNKNOWN:		Type invalid or not known
 * @EBITDO_DEVICE_KIND_BOOTLOADER:	Bootloader
 * @EBITDO_DEVICE_KIND_FC30:		FC30
 * @EBITDO_DEVICE_KIND_NES30:		NES30
 * @EBITDO_DEVICE_KIND_SFC30:		SFC30
 * @EBITDO_DEVICE_KIND_SNES30:		SNES30
 * @EBITDO_DEVICE_KIND_FC30PRO:		FC30PRO
 * @EBITDO_DEVICE_KIND_NES30PRO:	NES30PRO
 * @EBITDO_DEVICE_KIND_FC30_ARCADE:	FC30 ARCADE
 *
 * The device type.
 **/
typedef enum {
	EBITDO_DEVICE_KIND_UNKNOWN,
	EBITDO_DEVICE_KIND_BOOTLOADER,
	EBITDO_DEVICE_KIND_FC30,
	EBITDO_DEVICE_KIND_NES30,
	EBITDO_DEVICE_KIND_SFC30,
	EBITDO_DEVICE_KIND_SNES30,
	EBITDO_DEVICE_KIND_FC30PRO,
	EBITDO_DEVICE_KIND_NES30PRO,
	EBITDO_DEVICE_KIND_FC30_ARCADE,
	/*< private >*/
	EBITDO_DEVICE_KIND_LAST
} EbitdoDeviceKind;

EbitdoDevice	*ebitdo_device_new		(GUsbDevice *usb_device);

/* helpers */
EbitdoDeviceKind ebitdo_device_kind_from_string	(const gchar	*kind);
const gchar	*ebitdo_device_kind_to_string	(EbitdoDeviceKind kind);

/* getters */
EbitdoDeviceKind ebitdo_device_get_kind		(EbitdoDevice	*device);
GUsbDevice	*ebitdo_device_get_usb_device	(EbitdoDevice	*device);
const gchar	*ebitdo_device_get_version	(EbitdoDevice	*device);
const guint32	*ebitdo_device_get_serial	(EbitdoDevice	*device);

/* object methods */
gboolean	 ebitdo_device_open		(EbitdoDevice	*device,
						 GError		**error);
gboolean	 ebitdo_device_close		(EbitdoDevice	*device,
						 GError		**error);
gboolean	 ebitdo_device_write_firmware	(EbitdoDevice	*device,
						 GBytes		*fw,
						 GError		**error);

G_END_DECLS

#endif /* __EBITDO_DEVICE_H */
