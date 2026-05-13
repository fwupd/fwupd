/*
 * Copyright 2022 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include <fwupdplugin.h>

#define FU_TYPE_POLKIT_AUTHORITY (fu_polkit_authority_get_type())
G_DECLARE_FINAL_TYPE(FuPolkitAuthority, fu_polkit_authority, FU, POLKIT_AUTHORITY, GObject)

typedef enum {
	FU_POLKIT_AUTHORITY_CHECK_FLAG_NONE = 0,
	FU_POLKIT_AUTHORITY_CHECK_FLAG_ALLOW_USER_INTERACTION = 1 << 0,
	FU_POLKIT_AUTHORITY_CHECK_FLAG_USER_IS_TRUSTED = 1 << 1,
} G_GNUC_FLAG_ENUM FuPolkitAuthorityCheckFlags;

FuPolkitAuthority *
fu_polkit_authority_new(void);
gboolean
fu_polkit_authority_load(FuPolkitAuthority *self, GError **error) G_GNUC_NON_NULL(1);

void
fu_polkit_authority_check(FuPolkitAuthority *self,
			  const gchar *sender,
			  const gchar *action_id,
			  FuPolkitAuthorityCheckFlags flags,
			  GCancellable *cancellable,
			  GAsyncReadyCallback callback,
			  gpointer user_data) G_GNUC_NON_NULL(1, 3);
gboolean
fu_polkit_authority_check_finish(FuPolkitAuthority *self, GAsyncResult *res, GError **error)
    G_GNUC_NON_NULL(1, 2);
