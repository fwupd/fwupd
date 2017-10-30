/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2015 Richard Hughes <richard@hughsie.com>
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

#ifndef __DFU_CONTEXT_H
#define __DFU_CONTEXT_H

#include <glib-object.h>
#include <gio/gio.h>
#include <gusb.h>

#include "fu-quirks.h"

#include "dfu-device.h"

G_BEGIN_DECLS

#define DFU_TYPE_CONTEXT (dfu_context_get_type ())
G_DECLARE_DERIVABLE_TYPE (DfuContext, dfu_context, DFU, CONTEXT, GObject)

struct _DfuContextClass
{
	GObjectClass		 parent_class;
	void			(*device_added)		(DfuContext	*context,
							 DfuDevice	*device);
	void			(*device_removed)	(DfuContext	*context,
							 DfuDevice	*device);
	void			(*device_changed)	(DfuContext	*context,
							 DfuDevice	*device);
};

DfuContext	*dfu_context_new			(void);
DfuContext	*dfu_context_new_full			(GUsbContext	*usb_ctx,
							 FuQuirks	*quirks);
gboolean	 dfu_context_enumerate			(DfuContext	*context,
							 GError		**error);
GPtrArray	*dfu_context_get_devices		(DfuContext	*context);
guint		 dfu_context_get_timeout		(DfuContext	*context);
void		 dfu_context_set_timeout		(DfuContext	*context,
							 guint		 timeout);
DfuDevice	*dfu_context_get_device_by_vid_pid	(DfuContext	*context,
							 guint16	 vid,
							 guint16	 pid,
							 GError		**error);
DfuDevice	*dfu_context_get_device_by_platform_id	(DfuContext	*context,
							 const gchar	*platform_id,
							 GError		**error);
DfuDevice	*dfu_context_get_device_default		(DfuContext	*context,
							 GError		**error);

G_END_DECLS

#endif /* __DFU_CONTEXT_H */
