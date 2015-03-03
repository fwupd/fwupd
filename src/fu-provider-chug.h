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

#ifndef __FU_PROVIDER_CHUG_H
#define __FU_PROVIDER_CHUG_H

#include <glib-object.h>

#include "fu-device.h"
#include "fu-provider.h"

G_BEGIN_DECLS

#define FU_TYPE_PROVIDER_CHUG		(fu_provider_chug_get_type ())
#define FU_PROVIDER_CHUG(o)		(G_TYPE_CHECK_INSTANCE_CAST ((o), FU_TYPE_PROVIDER_CHUG, FuProviderChug))
#define FU_IS_PROVIDER_CHUG(o)		(G_TYPE_CHECK_INSTANCE_TYPE ((o), FU_TYPE_PROVIDER_CHUG))

typedef struct _FuProviderChugPrivate	FuProviderChugPrivate;
typedef struct _FuProviderChug		FuProviderChug;
typedef struct _FuProviderChugClass	FuProviderChugClass;

struct _FuProviderChug
{
	FuProvider			 parent;
	FuProviderChugPrivate		*priv;
};

struct _FuProviderChugClass
{
	FuProviderClass			 parent_class;
};

GType		 fu_provider_chug_get_type	(void);
FuProvider	*fu_provider_chug_new		(void);

G_END_DECLS

#endif /* __FU_PROVIDER_CHUG_H */
