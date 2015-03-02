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

#ifndef __FU_DEVICE_H
#define __FU_DEVICE_H

#include <glib-object.h>

G_BEGIN_DECLS

#define FU_TYPE_DEVICE		(fu_device_get_type ())
#define FU_DEVICE(o)		(G_TYPE_CHECK_INSTANCE_CAST ((o), FU_TYPE_DEVICE, FuDevice))
#define FU_DEVICE_CLASS(k)	(G_TYPE_CHECK_CLASS_CAST((k), FU_TYPE_DEVICE, FuDeviceClass))
#define FU_IS_DEVICE(o)		(G_TYPE_CHECK_INSTANCE_TYPE ((o), FU_TYPE_DEVICE))
#define FU_IS_DEVICE_CLASS(k)	(G_TYPE_CHECK_CLASS_TYPE ((k), FU_TYPE_DEVICE))
#define FU_DEVICE_GET_CLASS(o)	(G_TYPE_INSTANCE_GET_CLASS ((o), FU_TYPE_DEVICE, FuDeviceClass))
#define FU_DEVICE_ERROR		fu_device_error_quark()

typedef struct _FuDevicePrivate	FuDevicePrivate;
typedef struct _FuDevice	FuDevice;
typedef struct _FuDeviceClass	FuDeviceClass;

struct _FuDevice
{
	 GObject		 parent;
	 FuDevicePrivate	*priv;
};

struct _FuDeviceClass
{
	GObjectClass		 parent_class;
};

GType		 fu_device_get_type			(void);
FuDevice	*fu_device_new				(void);

/* accessors */
GVariant	*fu_device_to_variant			(FuDevice	*device);
const gchar	*fu_device_get_id			(FuDevice	*device);
void		 fu_device_set_id			(FuDevice	*device,
							 const gchar	*id);
const gchar	*fu_device_get_metadata			(FuDevice	*device,
							 const gchar	*key);
void		 fu_device_set_metadata			(FuDevice	*device,
							 const gchar	*key,
							 const gchar	*value);
void		 fu_device_set_metadata_from_iter	(FuDevice	*device,
							 GVariantIter	*iter);

G_END_DECLS

#endif /* __FU_DEVICE_H */

