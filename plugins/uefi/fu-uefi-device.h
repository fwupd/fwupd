/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2018 Richard Hughes <richard@hughsie.com>
 * Copyright (C) 2015-2017 Peter Jones <pjones@redhat.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#ifndef __FU_UEFI_DEVICE_H
#define __FU_UEFI_DEVICE_H

#include <glib-object.h>

#include "fu-plugin.h"

G_BEGIN_DECLS

#define FU_TYPE_UEFI_DEVICE (fu_uefi_device_get_type ())
G_DECLARE_FINAL_TYPE (FuUefiDevice, fu_uefi_device, FU, UEFI_DEVICE, FuDevice)

typedef enum {
	FU_UEFI_DEVICE_KIND_UNKNOWN,
	FU_UEFI_DEVICE_KIND_SYSTEM_FIRMWARE,
	FU_UEFI_DEVICE_KIND_DEVICE_FIRMWARE,
	FU_UEFI_DEVICE_KIND_UEFI_DRIVER,
	FU_UEFI_DEVICE_KIND_FMP,
	FU_UEFI_DEVICE_KIND_LAST
} FuUefiDeviceKind;

typedef enum {
	FU_UEFI_DEVICE_STATUS_SUCCESS				= 0x00,
	FU_UEFI_DEVICE_STATUS_ERROR_UNSUCCESSFUL		= 0x01,
	FU_UEFI_DEVICE_STATUS_ERROR_INSUFFICIENT_RESOURCES	= 0x02,
	FU_UEFI_DEVICE_STATUS_ERROR_INCORRECT_VERSION		= 0x03,
	FU_UEFI_DEVICE_STATUS_ERROR_INVALID_FORMAT		= 0x04,
	FU_UEFI_DEVICE_STATUS_ERROR_AUTH_ERROR			= 0x05,
	FU_UEFI_DEVICE_STATUS_ERROR_PWR_EVT_AC			= 0x06,
	FU_UEFI_DEVICE_STATUS_ERROR_PWR_EVT_BATT		= 0x07,
	FU_UEFI_DEVICE_STATUS_LAST
} FuUefiDeviceStatus;

FuUefiDevice	*fu_uefi_device_new_from_entry		(const gchar	*entry_path);
FuUefiDevice	*fu_uefi_device_new_from_dev		(FuDevice	*dev);
gboolean	 fu_uefi_device_clear_status		(FuUefiDevice	*self,
							 GError		**error);
FuUefiDeviceKind fu_uefi_device_get_kind		(FuUefiDevice	*self);
const gchar	*fu_uefi_device_get_guid		(FuUefiDevice	*self);
guint32		 fu_uefi_device_get_version		(FuUefiDevice	*self);
guint32		 fu_uefi_device_get_version_lowest	(FuUefiDevice	*self);
guint32		 fu_uefi_device_get_version_error	(FuUefiDevice	*self);
guint32		 fu_uefi_device_get_capsule_flags	(FuUefiDevice	*self);
guint64		 fu_uefi_device_get_hardware_instance	(FuUefiDevice	*self);
FuUefiDeviceStatus fu_uefi_device_get_status		(FuUefiDevice	*self);
const gchar	*fu_uefi_device_status_to_string	(FuUefiDeviceStatus status);

G_END_DECLS

#endif /* __FU_UEFI_DEVICE_H */
