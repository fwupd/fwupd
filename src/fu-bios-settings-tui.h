/*
 * Copyright 2026 Advanced Micro Devices, Inc.
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include <fwupd.h>

G_BEGIN_DECLS

GHashTable *
fu_bios_settings_tui_run(GPtrArray *settings, GError **error) G_GNUC_NON_NULL(1);

G_END_DECLS
