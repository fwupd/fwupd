/*
 * Copyright 2025 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include "fwupd-json-node.h"
#include "fwupd-rs-json.h"

FwupdJsonNode *
fwupd_json_node_new_from_rust(FwupdRsJsonNode *rs) G_GNUC_NON_NULL(1) G_GNUC_WARN_UNUSED_RESULT;
FwupdRsJsonNode *
fwupd_json_node_get_rust(FwupdJsonNode *self) G_GNUC_NON_NULL(1);
