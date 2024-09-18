/*
 * Copyright 2017 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include <fwupdplugin.h>

#define FU_TYPE_DEVICE_LIST (fu_device_list_get_type())
G_DECLARE_FINAL_TYPE(FuDeviceList, fu_device_list, FU, DEVICE_LIST, GObject)

FuDeviceList *
fu_device_list_new(void);
void
fu_device_list_add(FuDeviceList *self, FuDevice *device) G_GNUC_NON_NULL(1, 2);
void
fu_device_list_remove(FuDeviceList *self, FuDevice *device) G_GNUC_NON_NULL(1, 2);
void
fu_device_list_remove_all(FuDeviceList *self) G_GNUC_NON_NULL(1);
GPtrArray *
fu_device_list_get_all(FuDeviceList *self) G_GNUC_NON_NULL(1);
GPtrArray *
fu_device_list_get_active(FuDeviceList *self) G_GNUC_NON_NULL(1);
FuDevice *
fu_device_list_get_old(FuDeviceList *self, FuDevice *device) G_GNUC_NON_NULL(1, 2);
FuDevice *
fu_device_list_get_by_id(FuDeviceList *self, const gchar *device_id, GError **error)
    G_GNUC_NON_NULL(1, 2);
FuDevice *
fu_device_list_get_by_guid(FuDeviceList *self, const gchar *guid, GError **error)
    G_GNUC_NON_NULL(1, 2);
gboolean
fu_device_list_wait_for_replug(FuDeviceList *self, GError **error) G_GNUC_NON_NULL(1);
void
fu_device_list_depsolve_order(FuDeviceList *self, FuDevice *device) G_GNUC_NON_NULL(1, 2);
