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

#ifndef __FU_PROVIDER_H
#define __FU_PROVIDER_H

#include <glib-object.h>

#include "fu-device.h"
#include "fu-provider.h"

G_BEGIN_DECLS

#define FU_TYPE_PROVIDER		(fu_provider_get_type ())
#define FU_PROVIDER(o)			(G_TYPE_CHECK_INSTANCE_CAST ((o), FU_TYPE_PROVIDER, FuProvider))
#define FU_PROVIDER_CLASS(k)		(G_TYPE_CHECK_CLASS_CAST((k), FU_TYPE_PROVIDER, FuProviderClass))
#define FU_IS_PROVIDER(o)		(G_TYPE_CHECK_INSTANCE_TYPE ((o), FU_TYPE_PROVIDER))
#define FU_IS_PROVIDER_CLASS(k)		(G_TYPE_CHECK_CLASS_TYPE ((k), FU_TYPE_PROVIDER))
#define FU_PROVIDER_GET_CLASS(o)	(G_TYPE_INSTANCE_GET_CLASS ((o), FU_TYPE_PROVIDER, FuProviderClass))

typedef struct _FuProvider		FuProvider;
typedef struct _FuProviderClass		FuProviderClass;

struct _FuProvider
{
	 GObject			 parent;
};

struct _FuProviderClass
{
	GObjectClass	parent_class;

	/* vfunc */
	gboolean	(*coldplug)		(FuProvider	*provider,
						 GError		**error);
	gboolean	(*update_offline)	(FuProvider	*provider,
						 FuDevice	*device,
						 gint		 fd,
						 GError		**error);

	/* signals */
	void		(* device_added)	(FuProvider	*provider,
						 FuDevice	*device);
	void		(* device_removed)	(FuProvider	*provider,
						 FuDevice	*device);
};

GType		 fu_provider_get_type		(void);
void		 fu_provider_emit_added		(FuProvider	*provider,
						 FuDevice	*device);
void		 fu_provider_emit_removed	(FuProvider	*provider,
						 FuDevice	*device);
gboolean	 fu_provider_coldplug		(FuProvider	*provider,
						 GError		**error);
gboolean	 fu_provider_update_offline	(FuProvider	*provider,
						 FuDevice	*device,
						 gint		 fd,
						 GError		**error);

G_END_DECLS

#endif /* __FU_PROVIDER_H */

