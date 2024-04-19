/*
 * Copyright (C) 2017 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#pragma once

#include "fwupd-request.h"

G_BEGIN_DECLS

GVariant *
fwupd_request_to_variant(FwupdRequest *self) G_GNUC_NON_NULL(1);

void
fwupd_request_emit_invalidate(FwupdRequest *self) G_GNUC_NON_NULL(1);

G_END_DECLS
