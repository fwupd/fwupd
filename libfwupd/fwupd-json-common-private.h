/*
 * Copyright 2025 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include "fwupd-json-common.h"

void
fwupd_json_indent(GString *str, guint depth) G_GNUC_NON_NULL(1);
