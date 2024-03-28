/*
 * Copyright 2022 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include <json-glib/json-glib.h>

#include "fwupd-build.h"
#include "fwupd-report.h"

G_BEGIN_DECLS

void
fwupd_report_to_json(FwupdReport *self, JsonBuilder *builder) G_GNUC_NON_NULL(1, 2);

G_END_DECLS
