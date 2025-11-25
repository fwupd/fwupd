/*
 * Copyright 2024 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include "fu-device-event.h"

const gchar *
fu_device_event_get_id(FuDeviceEvent *self) G_GNUC_NON_NULL(1);
gchar *
fu_device_event_build_id(const gchar *id) G_GNUC_NON_NULL(1);
