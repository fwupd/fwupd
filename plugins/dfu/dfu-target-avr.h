/*
 * Copyright (C) 2017 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#pragma once

#include <glib-object.h>
#include <gio/gio.h>

#include "dfu-target.h"

#define DFU_TYPE_TARGET_AVR (dfu_target_avr_get_type ())
G_DECLARE_DERIVABLE_TYPE (DfuTargetAvr, dfu_target_avr, DFU, TARGET_AVR, DfuTarget)

struct _DfuTargetAvrClass
{
	DfuTargetClass		 parent_class;
};

DfuTarget	*dfu_target_avr_new		(void);
