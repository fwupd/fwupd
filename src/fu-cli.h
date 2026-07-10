/*
 * Copyright 2026 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include "fu-cli-struct.h"
#include "fu-console.h"

#define FU_TYPE_CLI (fu_cli_get_type())
G_DECLARE_DERIVABLE_TYPE(FuCli, fu_cli, FU, CLI, GObject)

struct _FuCliClass {
	GObjectClass parent_class;
};

typedef gboolean (*FuCliCmdFunc)(FuCli *self, gchar **values, GError **error) G_GNUC_NON_NULL(1);

gboolean
fu_cli_has_arg_flag(FuCli *self, FuCliArgFlag arg_flag);
void
fu_cli_add_arg_flag(FuCli *self, FuCliArgFlag arg_flag);
FuConsole *
fu_cli_get_console(FuCli *self);
FwupdClient *
fu_cli_get_client(FuCli *self);
GCancellable *
fu_cli_get_cancellable(FuCli *self);
void
fu_cli_watch_sigint_start(FuCli *self);
void
fu_cli_watch_sigint_stop(FuCli *self);
void
fu_cli_loop_run(FuCli *self);
GOptionGroup *
fu_cli_get_option_group(FuCli *self);
void
fu_cli_cmd_array_add_common(FuCli *self);

void
fu_cli_cmd_array_add(FuCli *self,
		     const gchar *name,
		     const gchar *arguments,
		     const gchar *description,
		     FuCliCmdFunc callback) G_GNUC_NON_NULL(1, 2, 4);
gchar *
fu_cli_cmd_array_to_string(FuCli *self) G_GNUC_NON_NULL(1);
void
fu_cli_cmd_array_sort(FuCli *self) G_GNUC_NON_NULL(1);
gboolean
fu_cli_cmd_array_run(FuCli *self, const gchar *command, gchar **values, GError **error)
    G_GNUC_NON_NULL(1, 2);

void
fu_cli_print_error(FuCli *self, const GError *error) G_GNUC_NON_NULL(1);
FwupdInstallFlags
fu_cli_get_install_flags(FuCli *self) G_GNUC_NON_NULL(1);
GPtrArray *
fu_cli_device_array_filter(FuCli *self, GPtrArray *devices, GError **error) G_GNUC_NON_NULL(1, 2);
gboolean
fu_cli_device_match_flags(FuCli *self, FwupdDevice *device) G_GNUC_NON_NULL(1, 2);
gboolean
fu_cli_release_match_flags(FuCli *self, FwupdRelease *release) G_GNUC_NON_NULL(1, 2);
gboolean
fu_cli_device_match_protocol(FuCli *self, FwupdDevice *device) G_GNUC_NON_NULL(1, 2);
void
fu_cli_add_filter_device_include(FuCli *self, FwupdDeviceFlags device_flag) G_GNUC_NON_NULL(1);
void
fu_cli_add_filter_device_exclude(FuCli *self, FwupdDeviceFlags device_flag) G_GNUC_NON_NULL(1);
GPtrArray *
fu_cli_release_array_filter_flags(FuCli *self, GPtrArray *rels, GError **error)
    G_GNUC_NON_NULL(1, 2);
gint
fu_cli_main(FuCli *self, gint argc, gchar **argv) G_GNUC_NON_NULL(1);
