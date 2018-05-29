/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2016-2017 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#ifndef __LU_DEVICE_BOOTLOADER_NORDIC_H
#define __LU_DEVICE_BOOTLOADER_NORDIC_H

#include "lu-device-bootloader.h"

G_BEGIN_DECLS

#define LU_TYPE_DEVICE_BOOTLOADER_NORDIC (lu_device_bootloader_nordic_get_type ())
G_DECLARE_FINAL_TYPE (LuDeviceBootloaderNordic, lu_device_bootloader_nordic, LU, DEVICE_BOOTLOADER_NORDIC, LuDeviceBootloader)

G_END_DECLS

#endif /* __LU_DEVICE_BOOTLOADER_NORDIC_H */
