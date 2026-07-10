/*
 * Copyright 2026 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include "fu-console.h"

#define FU_TYPE_CLI (fu_cli_get_type())
G_DECLARE_DERIVABLE_TYPE(FuCli, fu_cli, FU, CLI, GObject)

struct _FuCliClass {
	GObjectClass parent_class;
	GPtrArray *(*get_plugins)(FuCli *self, GError **error);
};

typedef gboolean (*FuCliCmdFunc)(FuCli *cli, gchar **values, GError **error) G_GNUC_NON_NULL(1);

FuCli *
fu_cli_new(void);

gboolean
fu_cli_get_as_json(FuCli *self);
FuConsole *
fu_cli_get_console(FuCli *self);
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
