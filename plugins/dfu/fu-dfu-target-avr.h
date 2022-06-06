/*
 * Copyright (C) 2017 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#pragma once

#include "fu-dfu-target.h"

#define FU_TYPE_DFU_TARGET_AVR (fu_dfu_target_avr_get_type())
G_DECLARE_DERIVABLE_TYPE(FuDfuTargetAvr, fu_dfu_target_avr, FU, DFU_TARGET_AVR, FuDfuTarget)

struct _FuDfuTargetAvrClass {
	FuDfuTargetClass parent_class;
};

FuDfuTarget *
fu_dfu_target_avr_new(void);
