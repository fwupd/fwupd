/*
 * Copyright (C) 2020 Cypress Semiconductor Corporation.
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#pragma once

#include <glib-object.h>
#include "fu-device.h"

gboolean	fu_ccgx_hid_enable_mfg_mode (FuDevice *self, gint inf_num, GError **error);
