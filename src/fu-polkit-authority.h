/*
 * Copyright (C) 2022 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#pragma once

#include <fwupdplugin.h>

#define FU_TYPE_POLKIT_AUTHORITY (fu_polkit_authority_get_type())
G_DECLARE_FINAL_TYPE(FuPolkitAuthority, fu_polkit_authority, FU, POLKIT_AUTHORITY, GObject)

typedef enum {
	FU_POLKIT_AUTHORITY_CHECK_FLAG_NONE = 0,
	FU_POLKIT_AUTHORITY_CHECK_FLAG_ALLOW_USER_INTERACTION = 1 << 0,
	FU_POLKIT_AUTHORITY_CHECK_FLAG_USER_IS_TRUSTED = 1 << 1,
} FuPolkitAuthorityCheckFlags;

FuPolkitAuthority *
fu_polkit_authority_new(void);
gboolean
fu_polkit_authority_load(FuPolkitAuthority *self, GError **error);

void
fu_polkit_authority_check(FuPolkitAuthority *self,
			  const gchar *sender,
			  const gchar *action_id,
			  FuPolkitAuthorityCheckFlags flags,
			  GCancellable *cancellable,
			  GAsyncReadyCallback callback,
			  gpointer user_data);
gboolean
fu_polkit_authority_check_finish(FuPolkitAuthority *self, GAsyncResult *res, GError **error);
