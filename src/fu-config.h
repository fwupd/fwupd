/*
 * Copyright (C) 2017 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#pragma once

#include "fwupd-remote.h"

#define FU_TYPE_CONFIG (fu_config_get_type())
G_DECLARE_FINAL_TYPE(FuConfig, fu_config, FU, CONFIG, GObject)

FuConfig *
fu_config_new(void);
gboolean
fu_config_load(FuConfig *self, GError **error);
gboolean
fu_config_set_key_value(FuConfig *self, const gchar *key, const gchar *value, GError **error);

guint64
fu_config_get_archive_size_max(FuConfig *self);
guint
fu_config_get_idle_timeout(FuConfig *self);
GPtrArray *
fu_config_get_disabled_devices(FuConfig *self);
GPtrArray *
fu_config_get_disabled_plugins(FuConfig *self);
GArray *
fu_config_get_trusted_uids(FuConfig *self);
GPtrArray *
fu_config_get_approved_firmware(FuConfig *self);
GPtrArray *
fu_config_get_blocked_firmware(FuConfig *self);
guint
fu_config_get_uri_scheme_prio(FuConfig *self, const gchar *scheme);
gboolean
fu_config_get_update_motd(FuConfig *self);
gboolean
fu_config_get_enumerate_all_devices(FuConfig *self);
gboolean
fu_config_get_ignore_power(FuConfig *self);
gboolean
fu_config_get_only_trusted(FuConfig *self);
gboolean
fu_config_get_show_device_private(FuConfig *self);
gboolean
fu_config_get_allow_emulation(FuConfig *self);
const gchar *
fu_config_get_host_bkc(FuConfig *self);
const gchar *
fu_config_get_esp_location(FuConfig *self);
