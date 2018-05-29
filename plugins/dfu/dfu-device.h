/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2015-2017 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#ifndef __DFU_DEVICE_H
#define __DFU_DEVICE_H

#include <glib-object.h>
#include <gio/gio.h>
#include <gusb.h>

#include "fu-usb-device.h"

#include "dfu-common.h"
#include "dfu-target.h"
#include "dfu-firmware.h"

G_BEGIN_DECLS

#define DFU_TYPE_DEVICE (dfu_device_get_type ())
G_DECLARE_DERIVABLE_TYPE (DfuDevice, dfu_device, DFU, DEVICE, FuUsbDevice)

/**
 * DfuDeviceQuirks:
 * @DFU_DEVICE_QUIRK_NONE:			No device quirks
 * @DFU_DEVICE_QUIRK_IGNORE_POLLTIMEOUT:	Ignore the device download timeout
 * @DFU_DEVICE_QUIRK_FORCE_DFU_MODE:		Force DFU mode
 * @DFU_DEVICE_QUIRK_USE_ANY_INTERFACE:		Use any interface for DFU
 * @DFU_DEVICE_QUIRK_NO_PID_CHANGE:		Accept the same VID:PID when changing modes
 * @DFU_DEVICE_QUIRK_NO_GET_STATUS_UPLOAD:	Do not do GetStatus when uploading
 * @DFU_DEVICE_QUIRK_NO_DFU_RUNTIME:		No DFU runtime interface is provided
 * @DFU_DEVICE_QUIRK_ATTACH_UPLOAD_DOWNLOAD:	An upload or download is required for attach
 * @DFU_DEVICE_QUIRK_IGNORE_RUNTIME:		Device has broken DFU runtime support
 * @DFU_DEVICE_QUIRK_ACTION_REQUIRED:		User has to do something manually, e.g. press a button
 * @DFU_DEVICE_QUIRK_IGNORE_UPLOAD:		Uploading from the device is broken
 * @DFU_DEVICE_QUIRK_ATTACH_EXTRA_RESET:	Device needs resetting twice for attach
 * @DFU_DEVICE_QUIRK_LEGACY_PROTOCOL:		Use a legacy protocol version
 *
 * The workarounds for different devices.
 **/
typedef enum {
	DFU_DEVICE_QUIRK_NONE			= 0,
	DFU_DEVICE_QUIRK_IGNORE_POLLTIMEOUT	= (1 << 0),
	DFU_DEVICE_QUIRK_FORCE_DFU_MODE		= (1 << 1),
	DFU_DEVICE_QUIRK_USE_ANY_INTERFACE	= (1 << 2),
	DFU_DEVICE_QUIRK_NO_PID_CHANGE		= (1 << 4),
	DFU_DEVICE_QUIRK_NO_GET_STATUS_UPLOAD	= (1 << 5),
	DFU_DEVICE_QUIRK_NO_DFU_RUNTIME		= (1 << 6),
	DFU_DEVICE_QUIRK_ATTACH_UPLOAD_DOWNLOAD	= (1 << 7),
	DFU_DEVICE_QUIRK_IGNORE_RUNTIME		= (1 << 8),
	DFU_DEVICE_QUIRK_ACTION_REQUIRED	= (1 << 9),
	DFU_DEVICE_QUIRK_IGNORE_UPLOAD		= (1 << 10),
	DFU_DEVICE_QUIRK_ATTACH_EXTRA_RESET	= (1 << 11),
	DFU_DEVICE_QUIRK_LEGACY_PROTOCOL	= (1 << 12),
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
	FuUsbDeviceClass	 parent_class;
	void			(*status_changed)	(DfuDevice	*device,
							 DfuStatus	 status);
	void			(*state_changed)	(DfuDevice	*device,
							 DfuState	 state);
	void			(*percentage_changed)	(DfuDevice	*device,
							 guint		 percentage);
	void			(*action_changed)	(DfuDevice	*device,
							 FwupdStatus	 action);
};

DfuDevice	*dfu_device_new				(GUsbDevice	*usb_device);
const gchar	*dfu_device_get_platform_id		(DfuDevice	*device);
GPtrArray	*dfu_device_get_targets			(DfuDevice	*device);
DfuTarget	*dfu_device_get_target_by_alt_setting	(DfuDevice	*device,
							 guint8		 alt_setting,
							 GError		**error);
DfuTarget	*dfu_device_get_target_by_alt_name	(DfuDevice	*device,
							 const gchar	*alt_name,
							 GError		**error);
const gchar	*dfu_device_get_chip_id			(DfuDevice	*device);
void		 dfu_device_set_chip_id			(DfuDevice	*device,
							 const gchar	*chip_id);
guint16		 dfu_device_get_runtime_vid		(DfuDevice	*device);
guint16		 dfu_device_get_runtime_pid		(DfuDevice	*device);
guint16		 dfu_device_get_runtime_release		(DfuDevice	*device);
guint16		 dfu_device_get_vid			(DfuDevice	*device);
guint16		 dfu_device_get_pid			(DfuDevice	*device);
guint16		 dfu_device_get_release			(DfuDevice	*device);
gboolean	 dfu_device_reset			(DfuDevice	*device,
							 GError		**error);
gboolean	 dfu_device_attach			(DfuDevice	*device,
							 GError		**error);
gboolean	 dfu_device_wait_for_replug		(DfuDevice	*device,
							 guint		 timeout,
							 GError		**error);
DfuFirmware	*dfu_device_upload			(DfuDevice	*device,
							 DfuTargetTransferFlags flags,
							 GError		**error);
gboolean	 dfu_device_download			(DfuDevice	*device,
							 DfuFirmware	*firmware,
							 DfuTargetTransferFlags flags,
							 GError		**error);
gboolean	 dfu_device_refresh			(DfuDevice	*device,
							 GError		**error);
gboolean	 dfu_device_refresh_and_clear		(DfuDevice	*device,
							 GError		**error);
gboolean	 dfu_device_detach			(DfuDevice	*device,
							 GError		**error);
gboolean	 dfu_device_abort			(DfuDevice	*device,
							 GError		**error);
gboolean	 dfu_device_clear_status		(DfuDevice	*device,
							 GError		**error);

guint8		 dfu_device_get_interface		(DfuDevice	*device);
gboolean	 dfu_device_is_runtime			(DfuDevice	*device);
DfuState	 dfu_device_get_state			(DfuDevice	*device);
DfuStatus	 dfu_device_get_status			(DfuDevice	*device);
guint16		 dfu_device_get_transfer_size		(DfuDevice	*device);
guint16		 dfu_device_get_version			(DfuDevice	*device);
guint		 dfu_device_get_timeout			(DfuDevice	*device);
gboolean	 dfu_device_can_upload			(DfuDevice	*device);
gboolean	 dfu_device_can_download		(DfuDevice	*device);

gboolean	 dfu_device_has_attribute		(DfuDevice	*device,
							 DfuDeviceAttributes attribute);
void		 dfu_device_remove_attribute		(DfuDevice	*device,
							 DfuDeviceAttributes attribute);
gboolean	 dfu_device_has_quirk			(DfuDevice	*device,
							 DfuDeviceQuirks quirk);

void		 dfu_device_set_transfer_size		(DfuDevice	*device,
							 guint16	 transfer_size);
void		 dfu_device_set_timeout			(DfuDevice	*device,
							 guint		 timeout_ms);
void		 dfu_device_set_usb_context		(DfuDevice	*device,
							 GUsbContext	*quirks);
GUsbContext	*dfu_device_get_usb_context		(DfuDevice	*device);

G_END_DECLS

#endif /* __DFU_DEVICE_H */
