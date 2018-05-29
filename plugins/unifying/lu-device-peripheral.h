/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2017 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#ifndef __LU_DEVICE_PERIPHERAL_H
#define __LU_DEVICE_PERIPHERAL_H

#include "lu-device.h"

G_BEGIN_DECLS

#define LU_TYPE_DEVICE_PERIPHERAL (lu_device_peripheral_get_type ())
G_DECLARE_FINAL_TYPE (LuDevicePeripheral, lu_device_peripheral, LU, DEVICE_PERIPHERAL, LuDevice)

G_END_DECLS

#endif /* __LU_DEVICE_PERIPHERAL_H */
