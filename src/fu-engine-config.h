/*
 * Copyright 2017 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include <fwupdplugin.h>

#include "fu-engine-struct.h"

#define FU_TYPE_ENGINE_CONFIG (fu_engine_config_get_type())
G_DECLARE_FINAL_TYPE(FuEngineConfig, fu_engine_config, FU, ENGINE_CONFIG, FuConfig)

FuEngineConfig *
fu_engine_config_new(void);
guint64
fu_engine_config_get_archive_size_max(FuEngineConfig *self) G_GNUC_NON_NULL(1);
guint
fu_engine_config_get_idle_timeout(FuEngineConfig *self) G_GNUC_NON_NULL(1);
GPtrArray *
fu_engine_config_get_disabled_devices(FuEngineConfig *self) G_GNUC_NON_NULL(1);
GPtrArray *
fu_engine_config_get_disabled_plugins(FuEngineConfig *self) G_GNUC_NON_NULL(1);
GArray *
fu_engine_config_get_trusted_uids(FuEngineConfig *self) G_GNUC_NON_NULL(1);
GPtrArray *
fu_engine_config_get_trusted_reports(FuEngineConfig *self) G_GNUC_NON_NULL(1);
GPtrArray *
fu_engine_config_get_approved_firmware(FuEngineConfig *self) G_GNUC_NON_NULL(1);
GPtrArray *
fu_engine_config_get_blocked_firmware(FuEngineConfig *self) G_GNUC_NON_NULL(1);
guint
fu_engine_config_get_uri_scheme_prio(FuEngineConfig *self, const gchar *scheme)
    G_GNUC_NON_NULL(1, 2);
gboolean
fu_engine_config_get_update_motd(FuEngineConfig *self) G_GNUC_NON_NULL(1);
gboolean
fu_engine_config_get_enumerate_all_devices(FuEngineConfig *self) G_GNUC_NON_NULL(1);
gboolean
fu_engine_config_get_ignore_power(FuEngineConfig *self) G_GNUC_NON_NULL(1);
gboolean
fu_engine_config_get_only_trusted(FuEngineConfig *self) G_GNUC_NON_NULL(1);
gboolean
fu_engine_config_get_show_device_private(FuEngineConfig *self) G_GNUC_NON_NULL(1);
gboolean
fu_engine_config_get_test_devices(FuEngineConfig *self) G_GNUC_NON_NULL(1);
gboolean
fu_engine_config_get_ignore_requirements(FuEngineConfig *self) G_GNUC_NON_NULL(1);
gboolean
fu_engine_config_get_release_dedupe(FuEngineConfig *self) G_GNUC_NON_NULL(1);
FuReleasePriority
fu_engine_config_get_release_priority(FuEngineConfig *self) G_GNUC_NON_NULL(1);
FuP2pPolicy
fu_engine_config_get_p2p_policy(FuEngineConfig *self) G_GNUC_NON_NULL(1);
const gchar *
fu_engine_config_get_host_bkc(FuEngineConfig *self) G_GNUC_NON_NULL(1);
const gchar *
fu_engine_config_get_esp_location(FuEngineConfig *self) G_GNUC_NON_NULL(1);
