/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2017 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#ifndef __DFU_TARGET_STM_H
#define __DFU_TARGET_STM_H

#include <glib-object.h>
#include <gio/gio.h>

#include "dfu-target.h"

G_BEGIN_DECLS

#define DFU_TYPE_TARGET_STM (dfu_target_stm_get_type ())
G_DECLARE_DERIVABLE_TYPE (DfuTargetStm, dfu_target_stm, DFU, TARGET_STM, DfuTarget)

struct _DfuTargetStmClass
{
	DfuTargetClass		 parent_class;
};

DfuTarget	*dfu_target_stm_new		(void);

G_END_DECLS

#endif /* __DFU_TARGET_STM_H */
