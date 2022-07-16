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
fwupd_device_to_variant(FwupdDevice *self);
GVariant *
fwupd_device_to_variant_full(FwupdDevice *self, FwupdDeviceFlags flags);
void
fwupd_device_incorporate(FwupdDevice *self, FwupdDevice *donor);
void
fwupd_device_to_json(FwupdDevice *self, JsonBuilder *builder);
void
fwupd_device_to_json_full(FwupdDevice *self, JsonBuilder *builder, FwupdDeviceFlags flags);
gboolean
fwupd_device_from_json(FwupdDevice *self, JsonNode *json_node, GError **error);

G_END_DECLS
