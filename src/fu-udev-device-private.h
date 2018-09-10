/*
 * Copyright (C) 2017-2018 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#ifndef __FU_UDEV_DEVICE_PRIVATE_H
#define __FU_UDEV_DEVICE_PRIVATE_H

#include "fu-udev-device.h"

G_BEGIN_DECLS

void		 fu_udev_device_emit_changed		(FuUdevDevice	*self);

G_END_DECLS

#endif /* __FU_UDEV_DEVICE_PRIVATE_H */
