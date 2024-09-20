/*
 * Copyright 2017 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include <json-glib/json-glib.h>

#include "fwupd-device.h"

G_BEGIN_DECLS

void
fwupd_device_incorporate(FwupdDevice *self, FwupdDevice *donor) G_GNUC_NON_NULL(1, 2);
void
fwupd_device_remove_children(FwupdDevice *self) G_GNUC_NON_NULL(1);

G_END_DECLS
