/*
 * Copyright (C) 2018 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#ifndef __FU_MM_DEVICE_H
#define __FU_MM_DEVICE_H

#include <libmm-glib.h>

#include "fu-plugin.h"

G_BEGIN_DECLS

#define FU_TYPE_MM_DEVICE (fu_mm_device_get_type ())
G_DECLARE_FINAL_TYPE (FuMmDevice, fu_mm_device, FU, MM_DEVICE, FuDevice)

FuMmDevice	*fu_mm_device_new		(MMManager	*manager,
						 MMObject	*omodem);

G_END_DECLS

#endif /* __FU_MM_DEVICE_H */
