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

void
fu_device_event_set_str(FuDeviceEvent *self, const gchar *key, const gchar *value)
    G_GNUC_NON_NULL(1, 2);
const gchar *
fu_device_event_get_str(FuDeviceEvent *self, const gchar *key, GError **error)
    G_GNUC_NON_NULL(1, 2);
void
fu_device_event_set_i64(FuDeviceEvent *self, const gchar *key, gint64 value) G_GNUC_NON_NULL(1, 2);
gint64
fu_device_event_get_i64(FuDeviceEvent *self, const gchar *key, GError **error)
    G_GNUC_NON_NULL(1, 2);
void
fu_device_event_set_bytes(FuDeviceEvent *self, const gchar *key, GBytes *value)
    G_GNUC_NON_NULL(1, 2, 3);
void
fu_device_event_set_data(FuDeviceEvent *self, const gchar *key, const guint8 *buf, gsize bufsz)
    G_GNUC_NON_NULL(1, 2);
GBytes *
fu_device_event_get_bytes(FuDeviceEvent *self, const gchar *key, GError **error)
    G_GNUC_NON_NULL(1, 2);
gboolean
fu_device_event_copy_data(FuDeviceEvent *self,
			  const gchar *key,
			  guint8 *buf,
			  gsize bufsz,
			  gsize *actual_length,
			  GError **error) G_GNUC_NON_NULL(1, 2);
