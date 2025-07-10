/*
 * Copyright 2021 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include <fwupdplugin.h>

#define FU_TYPE_LOGITECH_BULKCONTROLLER_DEVICE (fu_logitech_bulkcontroller_device_get_type())
G_DECLARE_FINAL_TYPE(FuLogitechBulkcontrollerDevice,
		     fu_logitech_bulkcontroller_device,
		     FU,
		     LOGITECH_BULKCONTROLLER_DEVICE,
		     FuUsbDevice)

extern GQuark FU_LOGITECH_BULKCONTROLLER_DEVICE_FLAG_CHECK_BUFFER_SIZE;
extern GQuark FU_LOGITECH_BULKCONTROLLER_DEVICE_FLAG_POST_INSTALL;
