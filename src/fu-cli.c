/*
 * Copyright 2026 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#define G_LOG_DOMAIN "FuCli"

#include "config.h"

#include "fu-cli.h"

G_DEFINE_TYPE(FuCli, fu_cli, G_TYPE_OBJECT)

static void
fu_cli_init(FuCli *self)
{
}

static void
fu_cli_finalize(GObject *obj)
{
	G_OBJECT_CLASS(fu_cli_parent_class)->finalize(obj);
}

static void
fu_cli_class_init(FuCliClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS(klass);
	object_class->finalize = fu_cli_finalize;
}
