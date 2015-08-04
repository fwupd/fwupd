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

#ifndef __FU_PROVIDER_RPI_H
#define __FU_PROVIDER_RPI_H

#include <glib-object.h>

#include "fu-device.h"
#include "fu-provider.h"

G_BEGIN_DECLS

#define FU_TYPE_PROVIDER_RPI		(fu_provider_rpi_get_type ())
#define FU_PROVIDER_RPI(o)		(G_TYPE_CHECK_INSTANCE_CAST ((o), FU_TYPE_PROVIDER_RPI, FuProviderRpi))
#define FU_IS_PROVIDER_RPI(o)		(G_TYPE_CHECK_INSTANCE_TYPE ((o), FU_TYPE_PROVIDER_RPI))

typedef struct _FuProviderRpiPrivate	FuProviderRpiPrivate;
typedef struct _FuProviderRpi		FuProviderRpi;
typedef struct _FuProviderRpiClass	FuProviderRpiClass;

struct _FuProviderRpi
{
	FuProvider			 parent;
	FuProviderRpiPrivate		*priv;
};

struct _FuProviderRpiClass
{
	FuProviderClass			 parent_class;
};

GType		 fu_provider_rpi_get_type	(void);
FuProvider	*fu_provider_rpi_new		(void);
void		 fu_provider_rpi_set_fw_dir	(FuProviderRpi	*provider_rpi,
						 const gchar	*fw_dir);

G_END_DECLS

#endif /* __FU_PROVIDER_RPI_H */
