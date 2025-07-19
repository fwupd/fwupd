/*
 * Copyright 2021 Texas Instruments Incorporated
 * Copyright 2022 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later OR MIT
 */

#pragma once

#include <fwupdplugin.h>

#define FU_TI_TPS6598X_PD_MAX 2 /* devices */

gboolean
fu_ti_tps6598x_byte_array_is_nonzero(GByteArray *buf);
