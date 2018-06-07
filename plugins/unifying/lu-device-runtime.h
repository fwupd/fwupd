/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2016-2017 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#ifndef __LU_DEVICE_RUNTIME_H
#define __LU_DEVICE_RUNTIME_H

#include "lu-device.h"

G_BEGIN_DECLS

#define LU_TYPE_DEVICE_RUNTIME (lu_device_runtime_get_type ())
G_DECLARE_FINAL_TYPE (LuDeviceRuntime, lu_device_runtime, LU, DEVICE_RUNTIME, LuDevice)

G_END_DECLS

#endif /* __LU_DEVICE_RUNTIME_H */
