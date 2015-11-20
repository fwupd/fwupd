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

#ifndef __DFU_TARGET_H
#define __DFU_TARGET_H

#include <glib-object.h>
#include <gio/gio.h>
#include <gusb.h>

#include "dfu-image.h"

G_BEGIN_DECLS

#define DFU_TYPE_TARGET (dfu_target_get_type ())
G_DECLARE_DERIVABLE_TYPE (DfuTarget, dfu_target, DFU, TARGET, GUsbDevice)

struct _DfuTargetClass
{
	GUsbDeviceClass		 parent_class;
};

/**
 * DfuTargetOpenFlags:
 * @DFU_TARGET_OPEN_FLAG_NONE:			No flags set
 * @DFU_TARGET_OPEN_FLAG_NO_AUTO_REFRESH:	Do not do the initial GET_STATUS
 *
 * The optional flags used for opening the target.
 **/
typedef enum {
	DFU_TARGET_OPEN_FLAG_NONE		= 0,
	DFU_TARGET_OPEN_FLAG_NO_AUTO_REFRESH	= (1 << 0),
	/*< private >*/
	DFU_TARGET_OPEN_FLAG_LAST,
} DfuTargetOpenFlags;

/**
 * DfuTargetTransferFlags:
 * @DFU_TARGET_TRANSFER_FLAG_NONE:		No flags set
 * @DFU_TARGET_TRANSFER_FLAG_VERIFY:		Verify the download once complete
 * @DFU_TARGET_TRANSFER_FLAG_HOST_RESET:	Reset the bus when complete
 * @DFU_TARGET_TRANSFER_FLAG_BOOT_RUNTIME:	Boot to runtime when complete
 * @DFU_TARGET_TRANSFER_FLAG_DETACH:		Automatically detach and reset when in appIDLE mode
 * @DFU_TARGET_TRANSFER_FLAG_WILDCARD_VID:	Allow downloading images with wildcard VIDs
 * @DFU_TARGET_TRANSFER_FLAG_WILDCARD_PID:	Allow downloading images with wildcard PIDs
 *
 * The optional flags used for transfering firmware.
 **/
typedef enum {
	DFU_TARGET_TRANSFER_FLAG_NONE		= 0,
	DFU_TARGET_TRANSFER_FLAG_VERIFY		= (1 << 0),
	DFU_TARGET_TRANSFER_FLAG_HOST_RESET	= (1 << 1),
	DFU_TARGET_TRANSFER_FLAG_BOOT_RUNTIME	= (1 << 2),
	DFU_TARGET_TRANSFER_FLAG_DETACH		= (1 << 3),
	DFU_TARGET_TRANSFER_FLAG_WILDCARD_VID	= (1 << 4),
	DFU_TARGET_TRANSFER_FLAG_WILDCARD_PID	= (1 << 5),
	/*< private >*/
	DFU_TARGET_TRANSFER_FLAG_LAST,
} DfuTargetTransferFlags;

typedef void	(*DfuProgressCallback)			(DfuState	 state,
							 goffset	 current,
							 goffset	 total,
							 gpointer	 user_data);

gboolean	 dfu_target_open			(DfuTarget	*target,
							 DfuTargetOpenFlags flags,
							 GCancellable	*cancellable,
							 GError		**error);
gboolean	 dfu_target_close			(DfuTarget	*target,
							 GError		**error);
DfuMode		 dfu_target_get_mode			(DfuTarget	*target);
DfuState	 dfu_target_get_state			(DfuTarget	*target);
DfuStatus	 dfu_target_get_status			(DfuTarget	*target);
GPtrArray	*dfu_target_get_sectors			(DfuTarget	*target);
gboolean	 dfu_target_can_upload			(DfuTarget	*target);
gboolean	 dfu_target_can_download		(DfuTarget	*target);
gboolean	 dfu_target_refresh			(DfuTarget	*target,
							 GCancellable	*cancellable,
							 GError		**error);
gboolean	 dfu_target_detach			(DfuTarget	*target,
							 GCancellable	*cancellable,
							 GError		**error);
gboolean	 dfu_target_abort			(DfuTarget	*target,
							 GCancellable	*cancellable,
							 GError		**error);
gboolean	 dfu_target_clear_status		(DfuTarget	*target,
							 GCancellable	*cancellable,
							 GError		**error);
DfuImage	*dfu_target_upload			(DfuTarget	*target,
							 DfuTargetTransferFlags flags,
							 GCancellable	*cancellable,
							 DfuProgressCallback progress_cb,
							 gpointer	 progress_cb_data,
							 GError		**error);
gboolean	 dfu_target_download			(DfuTarget	*target,
							 DfuImage	*image,
							 DfuTargetTransferFlags flags,
							 GCancellable	*cancellable,
							 DfuProgressCallback progress_cb,
							 gpointer	 progress_cb_data,
							 GError		**error);
void		 dfu_target_set_timeout			(DfuTarget	*target,
							 guint		 timeout_ms);
guint8		 dfu_target_get_interface_number	(DfuTarget	*target);
guint8		 dfu_target_get_interface_alt_setting	(DfuTarget	*target);
const gchar	*dfu_target_get_interface_alt_name	(DfuTarget	*target);
guint16		 dfu_target_get_transfer_size		(DfuTarget	*target);
void		 dfu_target_set_transfer_size		(DfuTarget	*target,
							 guint16	 transfer_size);

G_END_DECLS

#endif /* __DFU_TARGET_H */
