/*
 * Copyright (C) 2017 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#pragma once

#include "fwupd-request.h"

G_BEGIN_DECLS

GVariant *
fwupd_request_to_variant(FwupdRequest *self);

G_END_DECLS
