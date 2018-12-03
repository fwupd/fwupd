/*
 * Copyright (C) 2018 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#ifndef __FU_FASTBOOT_DEVICE_H
#define __FU_FASTBOOT_DEVICE_H

#include "fu-plugin.h"

G_BEGIN_DECLS

#define FU_TYPE_FASTBOOT_DEVICE (fu_fastboot_device_get_type ())
G_DECLARE_FINAL_TYPE (FuFastbootDevice, fu_fastboot_device, FU, FASTBOOT_DEVICE, FuUsbDevice)

FuFastbootDevice	*fu_fastboot_device_new	(FuUsbDevice	*device);

G_END_DECLS

#endif /* __FU_FASTBOOT_DEVICE_H */
