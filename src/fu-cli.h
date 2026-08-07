/*
 * Copyright 2026 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include "fu-cli-struct.h"

G_BEGIN_DECLS

#define FU_TYPE_CLI (fu_cli_get_type())
G_DECLARE_DERIVABLE_TYPE(FuCli, fu_cli, FU, CLI, GObject)

struct _FuCliClass {
	GObjectClass parent_class;
};

G_END_DECLS
