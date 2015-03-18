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

typedef enum {
	FU_PROVIDER_UPDATE_FLAG_NONE		= 0,
	FU_PROVIDER_UPDATE_FLAG_OFFLINE		= 1,
	FU_PROVIDER_UPDATE_FLAG_ALLOW_REINSTALL	= 2,
	FU_PROVIDER_UPDATE_FLAG_ALLOW_OLDER	= 4,
	FU_PROVIDER_UPDATE_FLAG_LAST
} FuProviderFlags;

struct _FuProviderClass
{
	GObjectClass	parent_class;

	/* vfunc */
	const gchar	*(*get_name)		(FuProvider	*provider);
	gboolean	 (*coldplug)		(FuProvider	*provider,
						 GError		**error);
	gboolean	 (*update_online)	(FuProvider	*provider,
						 FuDevice	*device,
						 gint		 fd,
						 FuProviderFlags flags,
						 GError		**error);
	gboolean	 (*update_offline)	(FuProvider	*provider,
						 FuDevice	*device,
						 gint		 fd,
						 FuProviderFlags flags,
						 GError		**error);
	gboolean	 (*clear_results)	(FuProvider	*provider,
						 FuDevice	*device,
						 GError		**error);
	gboolean	 (*get_results)		(FuProvider	*provider,
						 FuDevice	*device,
						 GError		**error);

	/* signals */
	void		 (* device_added)	(FuProvider	*provider,
						 FuDevice	*device);
	void		 (* device_removed)	(FuProvider	*provider,
						 FuDevice	*device);
	void		 (* status_changed)	(FuProvider	*provider,
						 FuStatus	 status);
};

GType		 fu_provider_get_type		(void);
void		 fu_provider_device_add		(FuProvider	*provider,
						 FuDevice	*device);
void		 fu_provider_device_remove	(FuProvider	*provider,
						 FuDevice	*device);
void		 fu_provider_set_status		(FuProvider	*provider,
						 FuStatus	 status);
const gchar	*fu_provider_get_name		(FuProvider	*provider);
gboolean	 fu_provider_coldplug		(FuProvider	*provider,
						 GError		**error);
gboolean	 fu_provider_update		(FuProvider	*provider,
						 FuDevice	*device,
						 GInputStream	*stream_cab,
						 gint		 fd_fw,
						 FuProviderFlags flags,
						 GError		**error);
gboolean	 fu_provider_clear_results	(FuProvider	*provider,
						 FuDevice	*device,
						 GError		**error);
gboolean	 fu_provider_get_results	(FuProvider	*provider,
						 FuDevice	*device,
						 GError		**error);

G_END_DECLS

#endif /* __FU_PROVIDER_H */

