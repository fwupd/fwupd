/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2016-2017 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#ifndef __LU_DEVICE_BOOTLOADER_TEXAS_H
#define __LU_DEVICE_BOOTLOADER_TEXAS_H

#include "lu-device-bootloader.h"

G_BEGIN_DECLS

#define LU_TYPE_DEVICE_BOOTLOADER_TEXAS (lu_device_bootloader_texas_get_type ())
G_DECLARE_FINAL_TYPE (LuDeviceBootloaderTexas, lu_device_bootloader_texas, LU, DEVICE_BOOTLOADER_TEXAS, LuDeviceBootloader)

G_END_DECLS

#endif /* __LU_DEVICE_BOOTLOADER_TEXAS_H */
