/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2016 Richard Hughes <richard@hughsie.com>
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

#ifndef __FWUPD_CLIENT_H
#define __FWUPD_CLIENT_H

#include <glib-object.h>
#include <gio/gio.h>

#include "fwupd-enums.h"
#include "fwupd-result.h"

G_BEGIN_DECLS

#define FWUPD_TYPE_CLIENT (fwupd_client_get_type ())
G_DECLARE_DERIVABLE_TYPE (FwupdClient, fwupd_client, FWUPD, CLIENT, GObject)

struct _FwupdClientClass
{
	GObjectClass		 parent_class;
	void			(*changed)		(FwupdClient	*client);
	void			(*status_changed)	(FwupdClient	*client,
							 FwupdStatus	 status);
	void			(*device_added)		(FwupdClient	*client,
							 FwupdResult	*result);
	void			(*device_removed)	(FwupdClient	*client,
							 FwupdResult	*result);
	void			(*device_changed)	(FwupdClient	*client,
							 FwupdResult	*result);
	/*< private >*/
	void (*_fwupd_reserved1)	(void);
	void (*_fwupd_reserved2)	(void);
	void (*_fwupd_reserved3)	(void);
	void (*_fwupd_reserved4)	(void);
};

FwupdClient	*fwupd_client_new			(void);
gboolean	 fwupd_client_connect			(FwupdClient	*client,
							 GCancellable	*cancellable,
							 GError		**error);
GPtrArray	*fwupd_client_get_devices		(FwupdClient	*client,
							 GCancellable	*cancellable,
							 GError		**error);
GPtrArray	*fwupd_client_get_updates		(FwupdClient	*client,
							 GCancellable	*cancellable,
							 GError		**error);
GPtrArray	*fwupd_client_get_details_local		(FwupdClient	*client,
							 const gchar	*filename,
							 GCancellable	*cancellable,
							 GError		**error);
gboolean	 fwupd_client_verify			(FwupdClient	*client,
							 const gchar	*device_id,
							 GCancellable	*cancellable,
							 GError		**error);
gboolean	 fwupd_client_unlock			(FwupdClient	*client,
							 const gchar	*device_id,
							 GCancellable	*cancellable,
							 GError		**error);
gboolean	 fwupd_client_clear_results		(FwupdClient	*client,
							 const gchar	*device_id,
							 GCancellable	*cancellable,
							 GError		**error);
FwupdResult	*fwupd_client_get_results		(FwupdClient	*client,
							 const gchar	*device_id,
							 GCancellable	*cancellable,
							 GError		**error);
G_DEPRECATED_FOR(fwupd_client_get_details_local)
FwupdResult	*fwupd_client_get_details		(FwupdClient	*client,
							 const gchar	*filename,
							 GCancellable	*cancellable,
							 GError		**error);
gboolean	 fwupd_client_install			(FwupdClient	*client,
							 const gchar	*device_id,
							 const gchar	*filename,
							 FwupdInstallFlags install_flags,
							 GCancellable	*cancellable,
							 GError		**error);
gboolean	 fwupd_client_update_metadata		(FwupdClient	*client,
							 const gchar	*metadata_fn,
							 const gchar	*signature_fn,
							 GCancellable	*cancellable,
							 GError		**error);

G_END_DECLS

#endif /* __FWUPD_CLIENT_H */

