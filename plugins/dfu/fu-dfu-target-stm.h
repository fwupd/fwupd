/*
 * Copyright (C) 2017 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#pragma once

#include <glib-object.h>
#include <gio/gio.h>

#include "fu-dfu-target.h"

#define FU_TYPE_DFU_TARGET_STM (fu_dfu_target_stm_get_type ())
G_DECLARE_DERIVABLE_TYPE (FuDfuTargetStm, fu_dfu_target_stm, FU, DFU_TARGET_STM, FuDfuTarget)

struct _FuDfuTargetStmClass
{
	FuDfuTargetClass		 parent_class;
};

FuDfuTarget	*fu_dfu_target_stm_new		(void);
