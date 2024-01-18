/*
 * Copyright (C) 2017 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#pragma once

#include <fwupdplugin.h>

#define FU_TYPE_REMOTE (fu_remote_get_type())
G_DECLARE_FINAL_TYPE(FuRemote, fu_remote, FU, REMOTE, FwupdRemote)

gboolean
fwupd_remote_load_from_filename(FwupdRemote *self,
				const gchar *filename,
				GCancellable *cancellable,
				GError **error) G_GNUC_NON_NULL(1, 2);
gboolean
fwupd_remote_save_to_filename(FwupdRemote *self,
			      const gchar *filename,
			      GCancellable *cancellable,
			      GError **error) G_GNUC_NON_NULL(1, 2);

FwupdRemote *
fu_remote_new(void);
