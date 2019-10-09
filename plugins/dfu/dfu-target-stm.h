/*
 * Copyright (C) 2017 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#pragma once

#include <glib-object.h>
#include <gio/gio.h>

#include "dfu-target.h"

#define DFU_TYPE_TARGET_STM (dfu_target_stm_get_type ())
G_DECLARE_DERIVABLE_TYPE (DfuTargetStm, dfu_target_stm, DFU, TARGET_STM, DfuTarget)

struct _DfuTargetStmClass
{
	DfuTargetClass		 parent_class;
};

DfuTarget	*dfu_target_stm_new		(void);
