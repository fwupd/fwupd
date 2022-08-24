/*
 * Copyright (C) 2021 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#pragma once

#include "fu-context.h"
#include "fu-hwids.h"
#include "fu-quirks.h"

FuContext *
fu_context_new(void);
gboolean
fu_context_reload_bios_settings(FuContext *self, GError **error);
gboolean
fu_context_load_hwinfo(FuContext *self, GError **error);
gboolean
fu_context_load_quirks(FuContext *self, FuQuirksLoadFlags flags, GError **error);
void
fu_context_set_runtime_versions(FuContext *self, GHashTable *runtime_versions);
void
fu_context_set_compile_versions(FuContext *self, GHashTable *compile_versions);
void
fu_context_add_firmware_gtype(FuContext *self, const gchar *id, GType gtype);
GPtrArray *
fu_context_get_firmware_gtype_ids(FuContext *self);
GType
fu_context_get_firmware_gtype_by_id(FuContext *self, const gchar *id);
void
fu_context_add_udev_subsystem(FuContext *self, const gchar *subsystem);
GPtrArray *
fu_context_get_udev_subsystems(FuContext *self);
