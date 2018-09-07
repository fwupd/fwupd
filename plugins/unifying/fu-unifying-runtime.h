/*
 * Copyright (C) 2016-2018 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#ifndef __FU_UNIFYING_DEVICE_RUNTIME_H
#define __FU_UNIFYING_DEVICE_RUNTIME_H

#include "fu-udev-device.h"

G_BEGIN_DECLS

#define FU_TYPE_UNIFYING_RUNTIME (fu_unifying_runtime_get_type ())
G_DECLARE_FINAL_TYPE (FuUnifyingRuntime, fu_unifying_runtime, FU, UNIFYING_RUNTIME, FuUdevDevice)

G_END_DECLS

#endif /* __FU_UNIFYING_DEVICE_RUNTIME_H */
