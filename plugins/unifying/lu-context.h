/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2017 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#ifndef __LU_CONTEXT_H
#define __LU_CONTEXT_H

#include <glib-object.h>
#include <gusb.h>

#include "lu-device.h"

G_BEGIN_DECLS

#define LU_TYPE_CONTEXT (lu_context_get_type ())
G_DECLARE_FINAL_TYPE (LuContext, lu_context, LU, CONTEXT, GObject)

GPtrArray	*lu_context_get_devices			(LuContext	*ctx);
LuDevice	*lu_context_find_by_platform_id		(LuContext	*ctx,
							 const gchar	*platform_id,
							 GError **error);
void		 lu_context_coldplug			(LuContext	*ctx);
void		 lu_context_set_poll_interval		(LuContext	*ctx,
							 guint		 poll_interval);
void		 lu_context_set_supported		(LuContext	*ctx,
							 GPtrArray	*supported_guids);
gboolean	 lu_context_wait_for_replug		(LuContext	*ctx,
							 LuDevice	*device,
							 guint		 timeout_ms,
							 GError		**error);

LuContext	*lu_context_new				(GError		**error);
LuContext	*lu_context_new_full			(GUsbContext	*usb_ctx);

G_END_DECLS

#endif /* __LU_CONTEXT_H */
