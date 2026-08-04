/*
 * Copyright 2017 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include <fwupdplugin.h>

#include "fu-cli-struct.h"
#include "fu-console.h"

/* custom return codes */
#define EXIT_NOTHING_TO_DO 2
#define EXIT_NOT_FOUND	   3
#define EXIT_NOT_REACHABLE 101 /* ENETUNREACH */

/* this is only valid for tools */
#define FWUPD_ERROR_INVALID_ARGS (FWUPD_ERROR_LAST + 1)

gboolean
fu_cli_is_interesting_device(GPtrArray *devs, FwupdDevice *dev) G_GNUC_NON_NULL(1, 2);
gchar *
fu_cli_get_user_cache_path(const gchar *fn) G_GNUC_NON_NULL(1);
const gchar *
fu_cli_branch_for_display(const gchar *branch);
const gchar *
fu_cli_request_get_message(FwupdRequest *req) G_GNUC_NON_NULL(1);
gchar *
fu_cli_get_release_description_with_fallback(FwupdRelease *rel) G_GNUC_NON_NULL(1);

gchar *
fu_cli_device_to_string(FwupdClient *client, FwupdDevice *dev, guint idt) G_GNUC_NON_NULL(1, 2);
gchar *
fu_cli_remote_to_string(FwupdRemote *remote, guint idt) G_GNUC_NON_NULL(1);
gchar *
fu_cli_plugin_to_string(FwupdPlugin *plugin, guint idt) G_GNUC_NON_NULL(1);
gchar *
fu_cli_release_to_string(FwupdRelease *rel, guint idt) G_GNUC_NON_NULL(1);

gchar *
fu_cli_plugin_flag_to_string(FwupdPluginFlags plugin_flag);
const gchar *
fu_cli_release_flag_to_string(FwupdReleaseFlags release_flag);
const gchar *
fu_cli_request_flag_to_string(FwupdRequestFlags request_flag);

gchar *
fu_cli_device_problem_to_string(FwupdClient *client, FwupdDevice *dev, FwupdDeviceProblem problem)
    G_GNUC_NON_NULL(1, 2);
GPtrArray *
fu_cli_device_problems_to_strings(FwupdClient *client, FwupdDevice *dev) G_GNUC_NON_NULL(1, 2);
gchar *
fu_cli_security_attrs_to_string(GPtrArray *attrs, FuSecurityAttrToStringFlags flags)
    G_GNUC_NON_NULL(1);
gchar *
fu_cli_security_events_to_string(GPtrArray *events, FuSecurityAttrToStringFlags flags)
    G_GNUC_NON_NULL(1);
gchar *
fu_cli_security_issues_to_string(GPtrArray *devices) G_GNUC_NON_NULL(1);
gchar *
fu_cli_bios_setting_to_string(FwupdBiosSetting *setting, guint idt) G_GNUC_NON_NULL(1);

gboolean
fu_cli_update_shutdown(GError **error);
gboolean
fu_cli_update_reboot(GError **error);

gboolean
fu_cli_is_url(const gchar *perhaps_url) G_GNUC_NON_NULL(1);
gchar *
fu_cli_convert_description(const gchar *xml, GError **error) G_GNUC_NON_NULL(1);

void
fu_cli_print_json_object(FuConsole *console, FwupdJsonObject *json_obj) G_GNUC_NON_NULL(1, 2);
const gchar *
fu_cli_get_prgname(const gchar *argv0) G_GNUC_NON_NULL(1);
