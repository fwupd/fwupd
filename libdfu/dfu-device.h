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

#include "dfu-common.h"
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
	DFU_DEVICE_OPEN_FLAG_LAST
} DfuDeviceOpenFlags;

/**
 * DfuDeviceQuirks:
 * @DFU_DEVICE_QUIRK_NONE:			No device quirks
 * @DFU_DEVICE_QUIRK_IGNORE_POLLTIMEOUT:	Ignore the device download timeout
 * @DFU_DEVICE_QUIRK_FORCE_DFU_MODE:		Force DFU mode
 * @DFU_DEVICE_QUIRK_IGNORE_INVALID_VERSION:	Ignore invalid version numbers
 * @DFU_DEVICE_QUIRK_USE_PROTOCOL_ZERO:		Fix up the protocol number
 * @DFU_DEVICE_QUIRK_NO_PID_CHANGE:		Accept the same VID:PID when changing modes
 * @DFU_DEVICE_QUIRK_NO_GET_STATUS_UPLOAD:	Do not do GetStatus when uploading
 * @DFU_DEVICE_QUIRK_NO_DFU_RUNTIME:		No DFU runtime interface is provided
 * @DFU_DEVICE_QUIRK_ATTACH_UPLOAD_DOWNLOAD:	An upload or download is required for attach
 * @DFU_DEVICE_QUIRK_IGNORE_RUNTIME:		Device has broken DFU runtime support
 *
 * The workarounds for different devices.
 **/
typedef enum {
	DFU_DEVICE_QUIRK_NONE			= 0,
	DFU_DEVICE_QUIRK_IGNORE_POLLTIMEOUT	= (1 << 0),
	DFU_DEVICE_QUIRK_FORCE_DFU_MODE		= (1 << 1),
	DFU_DEVICE_QUIRK_IGNORE_INVALID_VERSION	= (1 << 2),
	DFU_DEVICE_QUIRK_USE_PROTOCOL_ZERO	= (1 << 3),
	DFU_DEVICE_QUIRK_NO_PID_CHANGE		= (1 << 4),
	DFU_DEVICE_QUIRK_NO_GET_STATUS_UPLOAD	= (1 << 5),
	DFU_DEVICE_QUIRK_NO_DFU_RUNTIME		= (1 << 6),
	DFU_DEVICE_QUIRK_ATTACH_UPLOAD_DOWNLOAD	= (1 << 7),
	DFU_DEVICE_QUIRK_IGNORE_RUNTIME		= (1 << 8),
	/*< private >*/
	DFU_DEVICE_QUIRK_LAST
} DfuDeviceQuirks;

/**
 * DfuDeviceAttributes:
 * @DFU_DEVICE_ATTRIBUTE_NONE:			No attributes set
 * @DFU_DEVICE_ATTRIBUTE_CAN_DOWNLOAD:		Can download from host->device
 * @DFU_DEVICE_ATTRIBUTE_CAN_UPLOAD:		Can upload from device->host
 * @DFU_DEVICE_ATTRIBUTE_MANIFEST_TOL:		Can answer GetStatus in manifest
 * @DFU_DEVICE_ATTRIBUTE_WILL_DETACH:		Will self-detach
 * @DFU_DEVICE_ATTRIBUTE_CAN_ACCELERATE:	Use a larger transfer size for speed
 *
 * The device DFU attributes.
 **/
typedef enum {
	DFU_DEVICE_ATTRIBUTE_NONE		= 0,
	DFU_DEVICE_ATTRIBUTE_CAN_DOWNLOAD	= (1 << 0),
	DFU_DEVICE_ATTRIBUTE_CAN_UPLOAD		= (1 << 1),
	DFU_DEVICE_ATTRIBUTE_MANIFEST_TOL	= (1 << 2),
	DFU_DEVICE_ATTRIBUTE_WILL_DETACH	= (1 << 3),
	DFU_DEVICE_ATTRIBUTE_CAN_ACCELERATE	= (1 << 7),
	/*< private >*/
	DFU_DEVICE_ATTRIBUTE_LAST
} DfuDeviceAttributes;

struct _DfuDeviceClass
{
	GObjectClass		 parent_class;
	void			(*status_changed)	(DfuDevice	*device,
							 DfuStatus	 status);
	void			(*state_changed)	(DfuDevice	*device,
							 DfuState	 state);
	void			(*percentage_changed)	(DfuDevice	*device,
							 guint		 percentage);
	/*< private >*/
	/* Padding for future expansion */
	void (*_dfu_device_reserved1) (void);
	void (*_dfu_device_reserved2) (void);
	void (*_dfu_device_reserved3) (void);
	void (*_dfu_device_reserved4) (void);
	void (*_dfu_device_reserved5) (void);
	void (*_dfu_device_reserved6) (void);
	void (*_dfu_device_reserved7) (void);
	void (*_dfu_device_reserved8) (void);
	void (*_dfu_device_reserved9) (void);
};

DfuDevice	*dfu_device_new				(GUsbDevice	*dev);
gboolean	 dfu_device_open			(DfuDevice	*device,
							 DfuDeviceOpenFlags flags,
							 GCancellable	*cancellable,
							 GError		**error);
gboolean	 dfu_device_close			(DfuDevice	*device,
							 GError		**error);
const gchar	*dfu_device_get_platform_id		(DfuDevice	*device);
GPtrArray	*dfu_device_get_targets			(DfuDevice	*device);
DfuTarget	*dfu_device_get_target_by_alt_setting	(DfuDevice	*device,
							 guint8		 alt_setting,
							 GError		**error);
DfuTarget	*dfu_device_get_target_by_alt_name	(DfuDevice	*device,
							 const gchar	*alt_name,
							 GError		**error);
const gchar	*dfu_device_get_display_name		(DfuDevice	*device);
guint16		 dfu_device_get_runtime_vid		(DfuDevice	*device);
guint16		 dfu_device_get_runtime_pid		(DfuDevice	*device);
guint16		 dfu_device_get_runtime_release		(DfuDevice	*device);
gboolean	 dfu_device_reset			(DfuDevice	*device,
							 GError		**error);
gboolean	 dfu_device_attach			(DfuDevice	*device,
							 GError		**error);
gboolean	 dfu_device_wait_for_replug		(DfuDevice	*device,
							 guint		 timeout,
							 GCancellable	*cancellable,
							 GError		**error);
DfuFirmware	*dfu_device_upload			(DfuDevice	*device,
							 DfuTargetTransferFlags flags,
							 GCancellable	*cancellable,
							 GError		**error);
gboolean	 dfu_device_download			(DfuDevice	*device,
							 DfuFirmware	*firmware,
							 DfuTargetTransferFlags flags,
							 GCancellable	*cancellable,
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
guint16		 dfu_device_get_version			(DfuDevice	*device);
guint		 dfu_device_get_timeout			(DfuDevice	*device);
gboolean	 dfu_device_can_upload			(DfuDevice	*device);
gboolean	 dfu_device_can_download		(DfuDevice	*device);

gboolean	 dfu_device_has_attribute		(DfuDevice	*device,
							 DfuDeviceAttributes attribute);
gboolean	 dfu_device_has_quirk			(DfuDevice	*device,
							 DfuDeviceQuirks quirk);

void		 dfu_device_set_transfer_size		(DfuDevice	*device,
							 guint16	 transfer_size);
void		 dfu_device_set_timeout			(DfuDevice	*device,
							 guint		 timeout_ms);

G_END_DECLS

#endif /* __DFU_DEVICE_H */
