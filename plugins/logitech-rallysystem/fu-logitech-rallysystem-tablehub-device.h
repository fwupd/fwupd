/*
 * Copyright 1999-2023 Logitech, Inc.
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include <fwupdplugin.h>

#define FU_TYPE_LOGITECH_RALLYSYSTEM_TABLEHUB_DEVICE                                               \
	fu_logitech_rallysystem_tablehub_device_get_type()
G_DECLARE_FINAL_TYPE(FuLogitechRallysystemTablehubDevice,
		     fu_logitech_rallysystem_tablehub_device,
		     FU,
		     LOGITECH_RALLYSYSTEM_TABLEHUB_DEVICE,
		     FuUsbDevice)
