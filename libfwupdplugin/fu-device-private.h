/*
 * Copyright (C) 2017 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#pragma once

#include <fu-device.h>
#include <xmlb.h>

#define fu_device_set_plugin(d, v) fwupd_device_set_plugin(FWUPD_DEVICE(d), v)

const gchar *
fu_device_internal_flag_to_string(FuDeviceInternalFlags flag);
FuDeviceInternalFlags
fu_device_internal_flag_from_string(const gchar *flag);

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
void
fu_device_set_alternate(FuDevice *self, FuDevice *alternate) G_GNUC_NON_NULL(1);
gboolean
fu_device_ensure_id(FuDevice *self, GError **error) G_GNUC_WARN_UNUSED_RESULT G_GNUC_NON_NULL(1);
void
fu_device_incorporate_from_component(FuDevice *self, XbNode *component) G_GNUC_NON_NULL(1, 2);
void
fu_device_replace(FuDevice *self, FuDevice *donor) G_GNUC_NON_NULL(1);
void
fu_device_ensure_from_component(FuDevice *self, XbNode *component) G_GNUC_NON_NULL(1, 2);
void
fu_device_ensure_from_release(FuDevice *self, XbNode *rel) G_GNUC_NON_NULL(1, 2);
void
fu_device_convert_instance_ids(FuDevice *self) G_GNUC_NON_NULL(1);
GPtrArray *
fu_device_get_possible_plugins(FuDevice *self) G_GNUC_NON_NULL(1);
void
fu_device_add_possible_plugin(FuDevice *self, const gchar *plugin) G_GNUC_NON_NULL(1, 2);
guint
fu_device_get_request_cnt(FuDevice *self, FwupdRequestKind request_kind) G_GNUC_NON_NULL(1);
guint64
fu_device_get_private_flags(FuDevice *self) G_GNUC_NON_NULL(1);
void
fu_device_set_private_flags(FuDevice *self, guint64 flag) G_GNUC_NON_NULL(1);
void
fu_device_set_progress(FuDevice *self, FuProgress *progress) G_GNUC_NON_NULL(1);
FuDeviceInternalFlags
fu_device_get_internal_flags(FuDevice *self) G_GNUC_NON_NULL(1);
void
fu_device_set_internal_flags(FuDevice *self, FuDeviceInternalFlags flags) G_GNUC_NON_NULL(1);
gboolean
fu_device_set_quirk_kv(FuDevice *self, const gchar *key, const gchar *value, GError **error)
    G_GNUC_NON_NULL(1, 2, 3);
