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

#ifndef __DFU_DEVICE_H
#define __DFU_DEVICE_H

#include <glib-object.h>
#include <gio/gio.h>
#include <gusb.h>

#include "dfu-target.h"
#include "dfu-firmware.h"

G_BEGIN_DECLS

#define DFU_TYPE_DEVICE (dfu_device_get_type ())
G_DECLARE_DERIVABLE_TYPE (DfuDevice, dfu_device, DFU, DEVICE, GObject)

struct _DfuDeviceClass
{
	GObjectClass		 parent_class;
};

DfuDevice	*dfu_device_new				(GUsbDevice	*dev);
gboolean	 dfu_device_open			(DfuDevice	*device,
							 GError		**error);
gboolean	 dfu_device_close			(DfuDevice	*device,
							 GError		**error);
GPtrArray	*dfu_device_get_targets			(DfuDevice	*device);
DfuTarget	*dfu_device_get_target_default		(DfuDevice	*device,
							 GError		**error);
DfuTarget	*dfu_device_get_target_by_alt_setting	(DfuDevice	*device,
							 guint8		 alt_setting,
							 GError		**error);
DfuTarget	*dfu_device_get_target_by_alt_name	(DfuDevice	*device,
							 const gchar	*alt_name,
							 GError		**error);
guint16		 dfu_device_get_runtime_vid		(DfuDevice	*device);
guint16		 dfu_device_get_runtime_pid		(DfuDevice	*device);
gboolean	 dfu_device_reset			(DfuDevice	*device,
							 GError		**error);
gboolean	 dfu_device_wait_for_replug		(DfuDevice	*device,
							 guint		 timeout,
							 GCancellable	*cancellable,
							 GError		**error);
DfuFirmware	*dfu_device_upload			(DfuDevice	*device,
							 DfuTargetTransferFlags flags,
							 GCancellable	*cancellable,
							 DfuProgressCallback progress_cb,
							 gpointer	 progress_cb_data,
							 GError		**error);
gboolean	 dfu_device_download			(DfuDevice	*device,
							 DfuFirmware	*firmware,
							 DfuTargetTransferFlags flags,
							 GCancellable	*cancellable,
							 DfuProgressCallback progress_cb,
							 gpointer	 progress_cb_data,
							 GError		**error);

G_END_DECLS

#endif /* __DFU_DEVICE_H */
