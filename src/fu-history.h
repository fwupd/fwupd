/*
 * Copyright 2015 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include <fwupdplugin.h>

#include "fu-release.h"

#define FU_TYPE_PENDING (fu_history_get_type())
G_DECLARE_FINAL_TYPE(FuHistory, fu_history, FU, HISTORY, GObject)

FuHistory *
fu_history_new(FuContext *ctx);

gboolean
fu_history_add_device(FuHistory *self, FuDevice *device, FuRelease *release, GError **error)
    G_GNUC_NON_NULL(1, 2, 3);
gboolean
fu_history_modify_device(FuHistory *self, FuDevice *device, GError **error) G_GNUC_NON_NULL(1, 2);
gboolean
fu_history_modify_device_release(FuHistory *self,
				 FuDevice *device,
				 FuRelease *release,
				 GError **error) G_GNUC_NON_NULL(1, 2, 3);
gboolean
fu_history_remove_device(FuHistory *self, FuDevice *device, GError **error) G_GNUC_NON_NULL(1, 2);
gboolean
fu_history_remove_all(FuHistory *self, GError **error) G_GNUC_NON_NULL(1);
FuDevice *
fu_history_get_device_by_id(FuHistory *self, const gchar *device_id, GError **error)
    G_GNUC_NON_NULL(1, 2);
GPtrArray *
fu_history_get_devices(FuHistory *self, GError **error) G_GNUC_NON_NULL(1);

gboolean
fu_history_clear_approved_firmware(FuHistory *self, GError **error) G_GNUC_NON_NULL(1);
gboolean
fu_history_add_approved_firmware(FuHistory *self, const gchar *checksum, GError **error)
    G_GNUC_NON_NULL(1, 2);
GPtrArray *
fu_history_get_approved_firmware(FuHistory *self, GError **error) G_GNUC_NON_NULL(1);
gboolean
fu_history_clear_blocked_firmware(FuHistory *self, GError **error) G_GNUC_NON_NULL(1);
gboolean
fu_history_add_blocked_firmware(FuHistory *self, const gchar *checksum, GError **error)
    G_GNUC_NON_NULL(1, 2);
GPtrArray *
fu_history_get_blocked_firmware(FuHistory *self, GError **error) G_GNUC_NON_NULL(1);
gboolean
fu_history_add_security_attribute(FuHistory *self,
				  const gchar *security_attr_json,
				  const gchar *hsi_score,
				  GError **error) G_GNUC_NON_NULL(1, 2, 3);
GPtrArray *
fu_history_get_security_attrs(FuHistory *self, guint limit, GError **error) G_GNUC_NON_NULL(1);

gboolean
fu_history_add_emulation_tag(FuHistory *self, const gchar *device_id, GError **error)
    G_GNUC_NON_NULL(1, 2);
gboolean
fu_history_remove_emulation_tag(FuHistory *self, const gchar *device_id, GError **error)
    G_GNUC_NON_NULL(1, 2);
gboolean
fu_history_has_emulation_tag(FuHistory *self, const gchar *device_id, GError **error)
    G_GNUC_NON_NULL(1);
