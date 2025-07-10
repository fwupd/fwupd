/*
 * Copyright 2024 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include <fwupdplugin.h>

#define FU_TYPE_LOGITECH_BULKCONTROLLER_CHILD (fu_logitech_bulkcontroller_child_get_type())
G_DECLARE_FINAL_TYPE(FuLogitechBulkcontrollerChild,
		     fu_logitech_bulkcontroller_child,
		     FU,
		     LOGITECH_BULKCONTROLLER_CHILD,
		     FuDevice)

#define FU_LOGITECH_BULKCONTROLLER_DEVICE_FLAG_PHERIPHERAL_UPDATE "pheripheral-update"
