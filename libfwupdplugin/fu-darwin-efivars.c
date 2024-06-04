/*
 * Copyright 2022 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#define G_LOG_DOMAIN "FuDarwinEfivars"

#include "config.h"

#include "fu-darwin-efivars.h"

struct _FuDarwinEfivars {
	FuEfivars parent_instance;
};

G_DEFINE_TYPE(FuDarwinEfivars, fu_darwin_efivars, FU_TYPE_EFIVARS)

static void
fu_darwin_efivars_init(FuDarwinEfivars *self)
{
}

static void
fu_darwin_efivars_class_init(FuDarwinEfivarsClass *klass)
{
}

FuEfivars *
fu_efivars_new(void)
{
	return FU_EFIVARS(g_object_new(FU_TYPE_DARWIN_EFIVARS, NULL));
}
