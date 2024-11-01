/*
 * Copyright 2024 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include "fu-device-event.h"

FuDeviceEvent *
fu_device_event_new(const gchar *id);

const gchar *
fu_device_event_get_id(FuDeviceEvent *self) G_GNUC_NON_NULL(1);
