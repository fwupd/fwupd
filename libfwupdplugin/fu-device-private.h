/*
 * Copyright 2017 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include <xmlb.h>

#include "fu-backend.h"
#include "fu-device-event.h"
#include "fu-device.h"

#define fu_device_set_plugin(d, v) fwupd_device_set_plugin(FWUPD_DEVICE(d), v)

void
fu_device_remove_children(FuDevice *self) G_GNUC_NON_NULL(1);
GPtrArray *
fu_device_get_parent_guids(FuDevice *self) G_GNUC_NON_NULL(1);
gboolean
fu_device_has_parent_guid(FuDevice *self, const gchar *guid) G_GNUC_NON_NULL(1, 2);
GPtrArray *
fu_device_get_parent_physical_ids(FuDevice *self) G_GNUC_NON_NULL(1);
gboolean
fu_device_has_parent_physical_id(FuDevice *self, const gchar *physical_id) G_GNUC_NON_NULL(1, 2);
GPtrArray *
fu_device_get_parent_backend_ids(FuDevice *self) G_GNUC_NON_NULL(1);
gboolean
fu_device_has_parent_backend_id(FuDevice *self, const gchar *backend_id) G_GNUC_NON_NULL(1, 2);
void
fu_device_set_parent(FuDevice *self, FuDevice *parent) G_GNUC_NON_NULL(1);
gint
fu_device_get_order(FuDevice *self) G_GNUC_NON_NULL(1);
void
fu_device_set_order(FuDevice *self, gint order) G_GNUC_NON_NULL(1);
const gchar *
fu_device_get_update_request_id(FuDevice *self) G_GNUC_NON_NULL(1);
void
fu_device_set_update_request_id(FuDevice *self, const gchar *update_request_id) G_GNUC_NON_NULL(1);
gboolean
fu_device_ensure_id(FuDevice *self, GError **error) G_GNUC_WARN_UNUSED_RESULT G_GNUC_NON_NULL(1);
void
fu_device_incorporate_from_component(FuDevice *self, XbNode *component) G_GNUC_NON_NULL(1, 2);
void
fu_device_replace(FuDevice *self, FuDevice *donor) G_GNUC_NON_NULL(1);
void
fu_device_ensure_from_component(FuDevice *self, XbNode *component) G_GNUC_NON_NULL(1, 2);
void
fu_device_convert_instance_ids(FuDevice *self) G_GNUC_NON_NULL(1);
GPtrArray *
fu_device_get_possible_plugins(FuDevice *self) G_GNUC_NON_NULL(1);
void
fu_device_add_possible_plugin(FuDevice *self, const gchar *plugin) G_GNUC_NON_NULL(1, 2);
guint
fu_device_get_request_cnt(FuDevice *self, FwupdRequestKind request_kind) G_GNUC_NON_NULL(1);
void
fu_device_set_progress(FuDevice *self, FuProgress *progress) G_GNUC_NON_NULL(1);
gboolean
fu_device_set_quirk_kv(FuDevice *self, const gchar *key, const gchar *value, GError **error)
    G_GNUC_NON_NULL(1, 2, 3);
void
fu_device_set_specialized_gtype(FuDevice *self, GType gtype) G_GNUC_NON_NULL(1);
void
fu_device_set_proxy_gtype(FuDevice *self, GType gtype) G_GNUC_NON_NULL(1);
gboolean
fu_device_has_counterpart_guid(FuDevice *self, const gchar *guid) G_GNUC_NON_NULL(1, 2);
GPtrArray *
fu_device_get_counterpart_guids(FuDevice *self) G_GNUC_NON_NULL(1);
gboolean
fu_device_is_updatable(FuDevice *self) G_GNUC_NON_NULL(1);
const gchar *
fu_device_get_custom_flags(FuDevice *self) G_GNUC_NON_NULL(1);
void
fu_device_set_custom_flags(FuDevice *self, const gchar *custom_flags) G_GNUC_NON_NULL(1);
void
fu_device_register_private_flag_safe(FuDevice *self, const gchar *flag);

void
fu_device_add_event(FuDevice *self, FuDeviceEvent *event);
void
fu_device_clear_events(FuDevice *self);
GPtrArray *
fu_device_get_events(FuDevice *self);
FuDeviceEvent *
fu_device_save_event(FuDevice *self, const gchar *id);
FuDeviceEvent *
fu_device_load_event(FuDevice *self, const gchar *id, GError **error);
void
fu_device_set_target(FuDevice *self, FuDevice *target);

FuBackend *
fu_device_get_backend(FuDevice *self);
void
fu_device_set_backend(FuDevice *self, FuBackend *backend);
