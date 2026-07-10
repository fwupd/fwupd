/*
 * Copyright 2026 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include <fwupdplugin.h>

#include "fu-console.h"

typedef struct FuUtil FuUtil;

typedef struct {
	GPtrArray *(*get_plugins)(FuUtil *self, GError **error);
} FuUtilImpl;

typedef struct FuUtilPrivate FuUtilPrivate;

struct FuUtil {
	FuUtilPrivate *priv;
	const FuUtilImpl *impl;
	FuConsole *console;
	gboolean as_json;
};

void
fu_util_impl_cmd_array_add_all(GPtrArray *cmd_array);

FuUtil *
fu_util_new(void);

void
fu_util_free(FuUtil *self);

G_DEFINE_AUTOPTR_CLEANUP_FUNC(FuUtil, fu_util_free)
