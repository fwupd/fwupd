/*
 * Copyright (C) 2015-2017 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#pragma once

#include <glib-object.h>
#include <gio/gio.h>
#include <gusb.h>

#include "fu-usb-device.h"

#include "dfu-common.h"
#include "dfu-target.h"
#include "dfu-firmware.h"

#define DFU_TYPE_DEVICE (dfu_device_get_type ())
G_DECLARE_DERIVABLE_TYPE (DfuDevice, dfu_device, DFU, DEVICE, FuUsbDevice)

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
gboolean	 dfu_device_reset			(DfuDevice	*device,
							 GError		**error);
DfuFirmware	*dfu_device_upload			(DfuDevice	*device,
							 DfuTargetTransferFlags flags,
							 GError		**error);
gboolean	 dfu_device_refresh			(DfuDevice	*device,
							 GError		**error);
gboolean	 dfu_device_refresh_and_clear		(DfuDevice	*device,
							 GError		**error);
gboolean	 dfu_device_abort			(DfuDevice	*device,
							 GError		**error);
gboolean	 dfu_device_clear_status		(DfuDevice	*device,
							 GError		**error);

guint8		 dfu_device_get_interface		(DfuDevice	*device);
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

void		 dfu_device_set_transfer_size		(DfuDevice	*device,
							 guint16	 transfer_size);
void		 dfu_device_set_timeout			(DfuDevice	*device,
							 guint		 timeout_ms);
void		 dfu_device_error_fixup			(DfuDevice	*device,
							 GError		**error);
guint		 dfu_device_get_download_timeout	(DfuDevice	*device);
gchar		*dfu_device_get_attributes_as_string	(DfuDevice	*device);
gboolean	 dfu_device_ensure_interface		(DfuDevice	*device,
							 GError		**error);
