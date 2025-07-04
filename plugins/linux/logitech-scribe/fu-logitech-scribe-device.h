/*
 * Copyright 2022 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include <fwupdplugin.h>

#define FU_TYPE_LOGITECH_SCRIBE_DEVICE (fu_logitech_scribe_device_get_type())
G_DECLARE_FINAL_TYPE(FuLogitechScribeDevice,
		     fu_logitech_scribe_device,
		     FU,
		     LOGITECH_SCRIBE_DEVICE,
		     FuV4lDevice)
