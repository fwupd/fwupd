/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2017 Richard Hughes <richard@hughsie.com>
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

#ifndef __FWUPD_REMOTE_PRIVATE_H
#define __FWUPD_REMOTE_PRIVATE_H

#include "fwupd-remote.h"

G_BEGIN_DECLS

FwupdRemote	*fwupd_remote_from_variant		(GVariant	*data);
GVariant	*fwupd_remote_to_variant		(FwupdRemote	*self);
gboolean	 fwupd_remote_load_from_filename	(FwupdRemote	*self,
							 const gchar	*filename,
							 GCancellable	*cancellable,
							 GError		**error);
void		 fwupd_remote_set_priority		(FwupdRemote	*self,
							 gint		 priority);
void		 fwupd_remote_set_mtime			(FwupdRemote	*self,
							 guint64	 mtime);
gchar		**fwupd_remote_get_order_after		(FwupdRemote	*self);
gchar		**fwupd_remote_get_order_before		(FwupdRemote	*self);

G_END_DECLS

#endif /* __FWUPD_REMOTE_PRIVATE_H */

