/*
 * Copyright (C) 2023 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#pragma once

#include "fu-config.h"

FuConfig *
fu_config_new(void);
gboolean
fu_config_load(FuConfig *self, GError **error);
