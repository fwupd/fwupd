/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2017 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
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
void		 fwupd_remote_set_agreement		(FwupdRemote	*self,
							 const gchar	*agreement);
void		 fwupd_remote_set_mtime			(FwupdRemote	*self,
							 guint64	 mtime);
gchar		**fwupd_remote_get_order_after		(FwupdRemote	*self);
gchar		**fwupd_remote_get_order_before		(FwupdRemote	*self);

G_END_DECLS

#endif /* __FWUPD_REMOTE_PRIVATE_H */

