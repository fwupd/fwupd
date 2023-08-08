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
fu_device_get_parent_guids(FuDevice *self);
gboolean
fu_device_has_parent_guid(FuDevice *self, const gchar *guid);
GPtrArray *
fu_device_get_parent_physical_ids(FuDevice *self);
gboolean
fu_device_has_parent_physical_id(FuDevice *self, const gchar *physical_id);
void
fu_device_set_parent(FuDevice *self, FuDevice *parent);
gint
fu_device_get_order(FuDevice *self);
void
fu_device_set_order(FuDevice *self, gint order);
const gchar *
fu_device_get_update_request_id(FuDevice *self);
void
fu_device_set_update_request_id(FuDevice *self, const gchar *update_request_id);
void
fu_device_set_alternate(FuDevice *self, FuDevice *alternate);
gboolean
fu_device_ensure_id(FuDevice *self, GError **error) G_GNUC_WARN_UNUSED_RESULT;
void
fu_device_incorporate_from_component(FuDevice *self, XbNode *component);
void
fu_device_replace(FuDevice *self, FuDevice *donor);
void
fu_device_ensure_from_component(FuDevice *self, XbNode *component);
void
fu_device_convert_instance_ids(FuDevice *self);
gchar *
fu_device_get_guids_as_str(FuDevice *self);
GPtrArray *
fu_device_get_possible_plugins(FuDevice *self);
void
fu_device_add_possible_plugin(FuDevice *self, const gchar *plugin);
guint
fu_device_get_request_cnt(FuDevice *self, FwupdRequestKind request_kind);
guint64
fu_device_get_private_flags(FuDevice *self);
void
fu_device_set_private_flags(FuDevice *self, guint64 flag);
void
fu_device_set_progress(FuDevice *self, FuProgress *progress);
FuDeviceInternalFlags
fu_device_get_internal_flags(FuDevice *self);
void
fu_device_set_internal_flags(FuDevice *self, FuDeviceInternalFlags flags);
gboolean
fu_device_set_quirk_kv(FuDevice *self, const gchar *key, const gchar *value, GError **error);
