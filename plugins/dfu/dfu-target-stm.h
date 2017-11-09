/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2017 Richard Hughes <richard@hughsie.com>
 *
 * Licensed under the GNU Lesser General Public License Version 2.1
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301 USA
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
