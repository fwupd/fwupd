/*
 * Copyright (C) 2017 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#pragma once

#include <fwupdplugin.h>

#define FU_TYPE_ENGINE_CONFIG (fu_engine_config_get_type())
G_DECLARE_FINAL_TYPE(FuEngineConfig, fu_engine_config, FU, ENGINE_CONFIG, FuConfig)

FuEngineConfig *
fu_engine_config_new(void);
guint64
fu_engine_config_get_archive_size_max(FuEngineConfig *self);
guint
fu_engine_config_get_idle_timeout(FuEngineConfig *self);
GPtrArray *
fu_engine_config_get_disabled_devices(FuEngineConfig *self);
GPtrArray *
fu_engine_config_get_disabled_plugins(FuEngineConfig *self);
GArray *
fu_engine_config_get_trusted_uids(FuEngineConfig *self);
GPtrArray *
fu_engine_config_get_trusted_reports(FuEngineConfig *self);
GPtrArray *
fu_engine_config_get_approved_firmware(FuEngineConfig *self);
GPtrArray *
fu_engine_config_get_blocked_firmware(FuEngineConfig *self);
guint
fu_engine_config_get_uri_scheme_prio(FuEngineConfig *self, const gchar *scheme);
gboolean
fu_engine_config_get_update_motd(FuEngineConfig *self);
gboolean
fu_engine_config_get_enumerate_all_devices(FuEngineConfig *self);
gboolean
fu_engine_config_get_ignore_power(FuEngineConfig *self);
gboolean
fu_engine_config_get_only_trusted(FuEngineConfig *self);
gboolean
fu_engine_config_get_show_device_private(FuEngineConfig *self);
gboolean
fu_engine_config_get_allow_emulation(FuEngineConfig *self);
gboolean
fu_engine_config_get_release_dedupe(FuEngineConfig *self);
const gchar *
fu_engine_config_get_host_bkc(FuEngineConfig *self);
const gchar *
fu_engine_config_get_esp_location(FuEngineConfig *self);
