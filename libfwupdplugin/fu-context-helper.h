/*
 * Copyright 2024 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include "fu-backend.h"
#include "fu-context.h"

/* this header exists to prevent an #include loop between fu-context.h and fu-backend.h */

void
fu_context_add_backend(FuContext *self, FuBackend *backend) G_GNUC_NON_NULL(1, 2);
FuBackend *
fu_context_get_backend_by_name(FuContext *self, const gchar *name, GError **error)
    G_GNUC_NON_NULL(1, 2);
