/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2016-2017 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#ifndef __FU_ALTOS_DEVICE_H
#define __FU_ALTOS_DEVICE_H

#include <glib-object.h>
#include <gusb.h>

#include "fu-plugin.h"

G_BEGIN_DECLS

#define FU_TYPE_ALTOS_DEVICE (fu_altos_device_get_type ())
G_DECLARE_FINAL_TYPE (FuAltosDevice, fu_altos_device, FU, ALTOS_DEVICE, FuUsbDevice)

typedef enum {
	FU_ALTOS_DEVICE_KIND_UNKNOWN,
	FU_ALTOS_DEVICE_KIND_BOOTLOADER,
	FU_ALTOS_DEVICE_KIND_CHAOSKEY,
	/*< private >*/
	FU_ALTOS_DEVICE_KIND_LAST
} FuAltosDeviceKind;

typedef enum {
	FU_ALTOS_DEVICE_WRITE_FIRMWARE_FLAG_NONE	= 0,
	FU_ALTOS_DEVICE_WRITE_FIRMWARE_FLAG_REBOOT	= 1 << 0,
	/*< private >*/
	FU_ALTOS_DEVICE_WRITE_FIRMWARE_FLAG_LAST
} FuAltosDeviceWriteFirmwareFlag;

FuAltosDevice	*fu_altos_device_new			(GUsbDevice *usb_device);
FuAltosDeviceKind fu_altos_device_kind_from_string	(const gchar	*kind);
const gchar	*fu_altos_device_kind_to_string		(FuAltosDeviceKind kind);
FuAltosDeviceKind fu_altos_device_get_kind		(FuAltosDevice	*device);
gboolean	 fu_altos_device_probe			(FuAltosDevice	*device,
							 GError		**error);

G_END_DECLS

#endif /* __FU_ALTOS_DEVICE_H */
