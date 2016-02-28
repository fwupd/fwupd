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

#define FU_TYPE_PROVIDER (fu_provider_get_type ())
G_DECLARE_DERIVABLE_TYPE (FuProvider, fu_provider, FU, PROVIDER, GObject)

typedef enum {
	FU_PROVIDER_UPDATE_FLAG_NONE		= 0,
	FU_PROVIDER_UPDATE_FLAG_OFFLINE		= 1,
	FU_PROVIDER_UPDATE_FLAG_ALLOW_REINSTALL	= 2,
	FU_PROVIDER_UPDATE_FLAG_ALLOW_OLDER	= 4,
	FU_PROVIDER_UPDATE_FLAG_LAST
} FuProviderFlags;

typedef enum {
	FU_PROVIDER_VERIFY_FLAG_NONE	= 0,
	FU_PROVIDER_VERIFY_FLAG_LAST
} FuProviderVerifyFlags;

struct _FuProviderClass
{
	GObjectClass	parent_class;

	/* vfunc */
	const gchar	*(*get_name)		(FuProvider	*provider);
	gboolean	 (*coldplug)		(FuProvider	*provider,
						 GError		**error);
	gboolean	 (*verify)		(FuProvider	*provider,
						 FuDevice	*device,
						 FuProviderVerifyFlags flags,
						 GError		**error);
	gboolean	 (*unlock)		(FuProvider	*provider,
						 FuDevice	*device,
						 GError		**error);
	gboolean	 (*update_online)	(FuProvider	*provider,
						 FuDevice	*device,
						 GBytes		*blob_fw,
						 FuProviderFlags flags,
						 GError		**error);
	gboolean	 (*update_offline)	(FuProvider	*provider,
						 FuDevice	*device,
						 GBytes		*blob_fw,
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
						 FwupdStatus	 status);
};

#define FU_OFFLINE_TRIGGER_FILENAME	FU_OFFLINE_DESTDIR "/system-update"

void		 fu_provider_device_add		(FuProvider	*provider,
						 FuDevice	*device);
void		 fu_provider_device_remove	(FuProvider	*provider,
						 FuDevice	*device);
void		 fu_provider_set_status		(FuProvider	*provider,
						 FwupdStatus	 status);
const gchar	*fu_provider_get_name		(FuProvider	*provider);
gboolean	 fu_provider_coldplug		(FuProvider	*provider,
						 GError		**error);
gboolean	 fu_provider_update		(FuProvider	*provider,
						 FuDevice	*device,
						 GBytes		*blob_cab,
						 GBytes		*blob_fw,
						 FuProviderFlags flags,
						 GError		**error);
gboolean	 fu_provider_verify		(FuProvider	*provider,
						 FuDevice	*device,
						 FuProviderVerifyFlags flags,
						 GError		**error);
gboolean	 fu_provider_unlock		(FuProvider	*provider,
						 FuDevice	*device,
						 GError		**error);
gboolean	 fu_provider_clear_results	(FuProvider	*provider,
						 FuDevice	*device,
						 GError		**error);
gboolean	 fu_provider_get_results	(FuProvider	*provider,
						 FuDevice	*device,
						 GError		**error);

G_END_DECLS

#endif /* __FU_PROVIDER_H */

