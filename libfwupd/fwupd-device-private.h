/*
 * Copyright (C) 2017 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#pragma once

#include <json-glib/json-glib.h>

#include "fwupd-device.h"

G_BEGIN_DECLS

GVariant *
fwupd_device_to_variant(FwupdDevice *self) G_GNUC_NON_NULL(1);
GVariant *
fwupd_device_to_variant_full(FwupdDevice *self, FwupdDeviceFlags flags) G_GNUC_NON_NULL(1);
void
fwupd_device_incorporate(FwupdDevice *self, FwupdDevice *donor) G_GNUC_NON_NULL(1, 2);
void
fwupd_device_to_json(FwupdDevice *self, JsonBuilder *builder) G_GNUC_NON_NULL(1, 2);
void
fwupd_device_to_json_full(FwupdDevice *self, JsonBuilder *builder, FwupdDeviceFlags flags)
    G_GNUC_NON_NULL(1, 2);
gboolean
fwupd_device_from_json(FwupdDevice *self, JsonNode *json_node, GError **error)
    G_GNUC_NON_NULL(1, 2);

G_END_DECLS
