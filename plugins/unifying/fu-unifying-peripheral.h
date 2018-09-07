/*
 * Copyright (C) 2017-2018 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#ifndef __FU_UNIFYING_PERIPHERAL_H
#define __FU_UNIFYING_PERIPHERAL_H

#include "fu-udev-device.h"

G_BEGIN_DECLS

#define FU_TYPE_UNIFYING_PERIPHERAL (fu_unifying_peripheral_get_type ())
G_DECLARE_FINAL_TYPE (FuUnifyingPeripheral, fu_unifying_peripheral, FU, UNIFYING_PERIPHERAL, FuUdevDevice)

G_END_DECLS

#endif /* __FU_UNIFYING_PERIPHERAL_H */
