/*
 * Copyright 2025 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include "fwupd-json-array.h"
#include "fwupd-rs-json.h"

FwupdJsonArray *
fwupd_json_array_new_from_rust(FwupdRsJsonArray *rs) G_GNUC_NON_NULL(1) G_GNUC_WARN_UNUSED_RESULT;
FwupdRsJsonArray *
fwupd_json_array_get_rust(FwupdJsonArray *self) G_GNUC_NON_NULL(1);
