/*
 * Copyright 2024 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include <fwupd.h>

#define FU_TYPE_DEVICE_EVENT (fu_device_event_get_type())
G_DECLARE_DERIVABLE_TYPE(FuDeviceEvent, fu_device_event, FU, DEVICE_EVENT, GObject)

struct _FuDeviceEventClass {
	GObjectClass parent_class;
};
