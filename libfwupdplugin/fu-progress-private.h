/*
 * Copyright 2024 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include "fu-progress.h"

G_BEGIN_DECLS

gdouble
fu_progress_get_global_fraction(FuProgress *self) G_GNUC_NON_NULL(1) G_GNUC_PURE;
void
fu_progress_sleep_idle(FuProgress *self, GMainContext *main_ctx, guint delay_ms) G_GNUC_NON_NULL(1);

G_END_DECLS
