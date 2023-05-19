/*
 * Copyright (C) 2020 Cypress Semiconductor Corporation.
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#pragma once

#include <glib-object.h>

#include "fu-ccgx-struct.h"

gchar *
fu_ccgx_version_to_string(guint32 val);
FuCcgxFwMode
fu_ccgx_fw_mode_get_alternate(FuCcgxFwMode val);
