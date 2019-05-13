/*
 * Copyright (C) 2018 Richard Hughes <richard@hughsie.com>
 * Copyright (C) 2015-2017 Peter Jones <pjones@redhat.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#pragma once

#include "fu-plugin.h"
#include "fu-flashrom-device.h"

G_BEGIN_DECLS

#define FU_TYPE_FLASHROM_DEVICE (fu_flashrom_device_get_type ())
G_DECLARE_FINAL_TYPE (FuFlashromDevice, fu_flashrom_device, FU, UEFI_DEVICE, FuDevice)

typedef enum {
	FU_FLASHROM_DEVICE_KIND_UNKNOWN,
	FU_FLASHROM_DEVICE_KIND_SYSTEM_FIRMWARE,
	FU_FLASHROM_DEVICE_KIND_LAST
} FuFlashromDeviceKind;


const gchar	*fu_flashrom_device_kind_to_string		(FuFlashromDeviceKind kind);

G_END_DECLS
