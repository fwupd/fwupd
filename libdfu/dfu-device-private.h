/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2015 Richard Hughes <richard@hughsie.com>
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

#ifndef __DFU_DEVICE_PRIVATE_H
#define __DFU_DEVICE_PRIVATE_H

#include <glib-object.h>
#include <gio/gio.h>
#include <gusb.h>

#include "dfu-device.h"

G_BEGIN_DECLS

#define DFU_DEVICE_REPLUG_TIMEOUT	5000	/* ms */

GUsbDevice	*dfu_device_get_usb_dev			(DfuDevice	*device);

gboolean	 dfu_device_has_dfuse_support		(DfuDevice	*device);

void		 dfu_device_error_fixup			(DfuDevice	*device,
							 GCancellable	*cancellable,
							 GError		**error);
guint		 dfu_device_get_download_timeout	(DfuDevice	*device);
gchar		*dfu_device_get_quirks_as_string	(DfuDevice	*device);
gboolean	 dfu_device_set_new_usb_dev		(DfuDevice	*device,
							 GUsbDevice	*dev,
							 GCancellable	*cancellable,
							 GError		**error);
gboolean	 dfu_device_ensure_interface		(DfuDevice	*device,
							 GError		**error);

G_END_DECLS

#endif /* __DFU_DEVICE_PRIVATE_H */
