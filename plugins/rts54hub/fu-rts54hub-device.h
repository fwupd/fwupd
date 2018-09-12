/*
 * Copyright (C) 2018 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#ifndef __FU_RTS54HUB_DEVICE_H
#define __FU_RTS54HUB_DEVICE_H

#include "fu-plugin.h"

G_BEGIN_DECLS

#define FU_TYPE_RTS54HUB_DEVICE (fu_rts54hub_device_get_type ())
G_DECLARE_FINAL_TYPE (FuRts54HubDevice, fu_rts54hub_device, FU, RTS54HUB_DEVICE, FuUsbDevice)

FuRts54HubDevice	*fu_rts54hub_device_new		(FuUsbDevice		*device);

G_END_DECLS

#endif /* __FU_RTS54HUB_DEVICE_H */
