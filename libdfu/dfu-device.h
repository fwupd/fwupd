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

/**
 * DfuDeviceOpenFlags:
 * @DFU_DEVICE_OPEN_FLAG_NONE:			No flags set
 * @DFU_DEVICE_OPEN_FLAG_NO_AUTO_REFRESH:	Do not do the initial GET_STATUS
 *
 * The optional flags used for opening the target.
 **/
typedef enum {
	DFU_DEVICE_OPEN_FLAG_NONE		= 0,
	DFU_DEVICE_OPEN_FLAG_NO_AUTO_REFRESH	= (1 << 0),
	/*< private >*/
	DFU_DEVICE_OPEN_FLAG_LAST,
} DfuDeviceOpenFlags;

struct _DfuDeviceClass
{
	GObjectClass		 parent_class;
};

DfuDevice	*dfu_device_new				(GUsbDevice	*dev);
gboolean	 dfu_device_open			(DfuDevice	*device,
							 DfuDeviceOpenFlags flags,
							 GCancellable	*cancellable,
							 GError		**error);
gboolean	 dfu_device_close			(DfuDevice	*device,
							 GError		**error);
GPtrArray	*dfu_device_get_targets			(DfuDevice	*device);
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
gboolean	 dfu_device_refresh			(DfuDevice	*device,
							 GCancellable	*cancellable,
							 GError		**error);
gboolean	 dfu_device_detach			(DfuDevice	*device,
							 GCancellable	*cancellable,
							 GError		**error);
gboolean	 dfu_device_abort			(DfuDevice	*device,
							 GCancellable	*cancellable,
							 GError		**error);
gboolean	 dfu_device_clear_status		(DfuDevice	*device,
							 GCancellable	*cancellable,
							 GError		**error);

guint8		 dfu_device_get_interface		(DfuDevice	*device);
DfuMode		 dfu_device_get_mode			(DfuDevice	*device);
DfuState	 dfu_device_get_state			(DfuDevice	*device);
DfuStatus	 dfu_device_get_status			(DfuDevice	*device);
guint16		 dfu_device_get_transfer_size		(DfuDevice	*device);
guint		 dfu_device_get_timeout			(DfuDevice	*device);
gboolean	 dfu_device_can_upload			(DfuDevice	*device);
gboolean	 dfu_device_can_download		(DfuDevice	*device);

void		 dfu_device_set_transfer_size		(DfuDevice	*device,
							 guint16	 transfer_size);
void		 dfu_device_set_timeout			(DfuDevice	*device,
							 guint		 timeout_ms);

G_END_DECLS

#endif /* __DFU_DEVICE_H */
