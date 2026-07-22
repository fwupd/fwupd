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

gchar *
fu_cli_plugin_flag_to_string(FwupdPluginFlags plugin_flag);
const gchar *
fu_cli_release_flag_to_string(FwupdReleaseFlags release_flag);
const gchar *
fu_cli_request_flag_to_string(FwupdRequestFlags request_flag);
gchar *
fu_cli_device_problem_to_string(FwupdClient *client, FwupdDevice *dev, FwupdDeviceProblem problem)
    G_GNUC_NON_NULL(1, 2);

void
fu_cli_print_json_object(FuConsole *console, FwupdJsonObject *json_obj) G_GNUC_NON_NULL(1, 2);
const gchar *
fu_cli_get_prgname(const gchar *argv0) G_GNUC_NON_NULL(1);
