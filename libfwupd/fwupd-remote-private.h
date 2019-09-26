/*
 * Copyright (C) 2017 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#pragma once

#include "fwupd-remote.h"

G_BEGIN_DECLS

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

void		 fwupd_remote_set_remotes_dir		(FwupdRemote	*self,
							 const gchar	*directory);
G_END_DECLS
