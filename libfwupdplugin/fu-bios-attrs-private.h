/*
 * Copyright (C) 2022 Mario Limonciello <mario.limonciello@amd.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#pragma once

#include "fu-bios-attrs.h"

gboolean
fu_bios_attrs_setup(FuBiosAttrs *self, GError **error);

GPtrArray *
fu_bios_attrs_get_all(FuBiosAttrs *self);

GVariant *
fu_bios_attrs_to_variant(FuBiosAttrs *self);
