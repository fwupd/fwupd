/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2015 Richard Hughes <richard@hughsie.com>
 *
 * Licensed under the GNU General Public License Version 2
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#ifndef __FU_PENDING_H
#define __FU_PENDING_H

#include <glib-object.h>

#include "fu-device.h"

G_BEGIN_DECLS

#define FU_TYPE_PENDING		(fu_pending_get_type ())
#define FU_PENDING(o)		(G_TYPE_CHECK_INSTANCE_CAST ((o), FU_TYPE_PENDING, FuPending))
#define FU_PENDING_CLASS(k)	(G_TYPE_CHECK_CLASS_CAST((k), FU_TYPE_PENDING, FuPendingClass))
#define FU_IS_PENDING(o)	(G_TYPE_CHECK_INSTANCE_TYPE ((o), FU_TYPE_PENDING))
#define FU_IS_PENDING_CLASS(k)	(G_TYPE_CHECK_CLASS_TYPE ((k), FU_TYPE_PENDING))
#define FU_PENDING_GET_CLASS(o)	(G_TYPE_INSTANCE_GET_CLASS ((o), FU_TYPE_PENDING, FuPendingClass))
#define FU_PENDING_ERROR	fu_pending_error_quark()

typedef struct _FuPendingPrivate	FuPendingPrivate;
typedef struct _FuPending	FuPending;
typedef struct _FuPendingClass	FuPendingClass;

struct _FuPending
{
	 GObject		 parent;
	 FuPendingPrivate	*priv;
};

struct _FuPendingClass
{
	GObjectClass		 parent_class;
};

typedef enum {
	FU_PENDING_STATE_UNKNOWN,
	FU_PENDING_STATE_SCHEDULED,
	FU_PENDING_STATE_SUCCESS,
	FU_PENDING_STATE_FAILED,
	/* private */
	FU_PENDING_STATE_LAST
} FuPendingState;

GType		 fu_pending_get_type			(void);
FuPending	*fu_pending_new				(void);
const gchar	*fu_pending_state_to_string		(FuPendingState	 state);

gboolean	 fu_pending_add_device			(FuPending	*pending,
							 FuDevice	*device,
							 GError		**error);
gboolean	 fu_pending_set_state			(FuPending	*pending,
							 FuDevice	*device,
							 FuPendingState	 state,
							 GError		**error);
gboolean	 fu_pending_set_error_msg			(FuPending	*pending,
							 FuDevice	*device,
							 const gchar	*error_msg,
							 GError		**error);
gboolean	 fu_pending_remove_device		(FuPending	*pending,
							 FuDevice	*device,
							 GError		**error);
FuDevice	*fu_pending_get_device			(FuPending	*pending,
							 const gchar	*device_id,
							 GError		**error);
GPtrArray	*fu_pending_get_devices			(FuPending	*pending,
							 GError		**error);

G_END_DECLS

#endif /* __FU_PENDING_H */

