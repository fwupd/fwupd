/*
 * Copyright 2017 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include "fwupd-request.h"

G_BEGIN_DECLS

void
fwupd_request_emit_invalidate(FwupdRequest *self) G_GNUC_NON_NULL(1);

G_END_DECLS
