/*
 * Copyright (C) 2018-2019 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#ifndef __FU_WACOM_AES_DEVICE_H
#define __FU_WACOM_AES_DEVICE_H

#include "fu-wacom-device.h"

G_BEGIN_DECLS

#define FU_TYPE_WACOM_AES_DEVICE (fu_wacom_aes_device_get_type ())
G_DECLARE_FINAL_TYPE (FuWacomAesDevice, fu_wacom_aes_device, FU, WACOM_AES_DEVICE, FuWacomDevice)

FuWacomAesDevice	*fu_wacom_aes_device_new	(FuUdevDevice	*device);

G_END_DECLS

#endif /* __FU_WACOM_AES_DEVICE_H */
