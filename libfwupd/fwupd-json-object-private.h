/*
 * Copyright 2025 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include "fwupd-json-object.h"
#include "fwupd-rs-json.h"

FwupdJsonObject *
fwupd_json_object_new_from_rust(FwupdRsJsonObject *rs) G_GNUC_NON_NULL(1) G_GNUC_WARN_UNUSED_RESULT;
FwupdRsJsonObject *
fwupd_json_object_get_rust(FwupdJsonObject *self) G_GNUC_NON_NULL(1);
