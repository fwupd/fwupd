/*
 * Copyright 2025 Harris Tai <harris_tai@pixart.com>
 * Copyright 2025 Micky Hsieh <micky_hsieh@pixart.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */
#include "config.h"

#include "fu-pxi-tp-struct.h"

/* put into .rodata; visible to all TUs */
const char PXI_TP_MAGIC[5] = "FWHD";
