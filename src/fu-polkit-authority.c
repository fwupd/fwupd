/*
 * Copyright (C) 2022 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#define G_LOG_DOMAIN "FuPolkitAuthority"

#include "config.h"

#ifdef HAVE_POLKIT
#include <polkit/polkit.h>
#endif

#include "fu-polkit-authority.h"

#ifdef HAVE_POLKIT
#ifndef HAVE_POLKIT_0_114
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunused-function"
G_DEFINE_AUTOPTR_CLEANUP_FUNC(PolkitAuthorizationResult, g_object_unref)
G_DEFINE_AUTOPTR_CLEANUP_FUNC(PolkitSubject, g_object_unref)
#pragma clang diagnostic pop
#endif /* HAVE_POLKIT_0_114 */
#endif /* HAVE_POLKIT */

struct _FuPolkitAuthority {
	GObject parent_instance;
#ifdef HAVE_POLKIT
	PolkitAuthority *pkauthority;
#endif
};

G_DEFINE_TYPE(FuPolkitAuthority, fu_polkit_authority, G_TYPE_OBJECT)

gboolean
fu_polkit_authority_check_finish(FuPolkitAuthority *self, GAsyncResult *res, GError **error)
{
	g_return_val_if_fail(FU_IS_POLKIT_AUTHORITY(self), FALSE);
	g_return_val_if_fail(g_task_is_valid(res, self), FALSE);
	g_return_val_if_fail(error == NULL || *error == NULL, FALSE);
	return g_task_propagate_boolean(G_TASK(res), error);
}

#ifdef HAVE_POLKIT
static void
fu_polkit_authority_check_cb(GObject *source_object, GAsyncResult *res, gpointer user_data)
{
	g_autoptr(GError) error_local = NULL;
	g_autoptr(GTask) task = G_TASK(user_data);
	g_autoptr(PolkitAuthorizationResult) auth = NULL;

	auth = polkit_authority_check_authorization_finish(POLKIT_AUTHORITY(source_object),
							   res,
							   &error_local);
	if (auth == NULL) {
		g_task_return_new_error(task,
					FWUPD_ERROR,
					FWUPD_ERROR_AUTH_FAILED,
					"Could not check for auth: %s",
					error_local->message);
		return;
	}

	/* did not auth */
	if (!polkit_authorization_result_get_is_authorized(auth)) {
		g_task_return_new_error(task,
					FWUPD_ERROR,
					FWUPD_ERROR_AUTH_FAILED,
					"Failed to obtain auth");
		return;
	}

	/* success */
	g_task_return_boolean(task, TRUE);
}
#endif

void
fu_polkit_authority_check(FuPolkitAuthority *self,
			  const gchar *sender,
			  const gchar *action_id,
			  FuPolkitAuthorityCheckFlags flags,
			  GCancellable *cancellable,
			  GAsyncReadyCallback callback,
			  gpointer user_data)
{
	g_autoptr(GTask) task = g_task_new(self, cancellable, callback, user_data);
#ifdef HAVE_POLKIT
	PolkitCheckAuthorizationFlags pkflags = POLKIT_CHECK_AUTHORIZATION_FLAGS_NONE;
	g_autofree gchar *owner = polkit_authority_get_owner(self->pkauthority);
#endif

	g_return_if_fail(FU_IS_POLKIT_AUTHORITY(self));
	g_return_if_fail(sender != NULL);
	g_return_if_fail(action_id != NULL);
	g_return_if_fail(callback != NULL);

#ifdef HAVE_POLKIT
	if (owner != NULL) {
		g_autoptr(PolkitSubject) pksubject = polkit_system_bus_name_new(sender);
		if (flags & FU_POLKIT_AUTHORITY_CHECK_FLAG_ALLOW_USER_INTERACTION)
			pkflags |= POLKIT_CHECK_AUTHORIZATION_FLAGS_ALLOW_USER_INTERACTION;
		polkit_authority_check_authorization(self->pkauthority,
						     pksubject,
						     action_id,
						     NULL, /* details */
						     pkflags,
						     cancellable,
						     fu_polkit_authority_check_cb,
						     g_steal_pointer(&task));
		return;
	}
#endif

	/* fallback to the caller being euid=0 */
	if ((flags & FU_POLKIT_AUTHORITY_CHECK_FLAG_USER_IS_TRUSTED) == 0) {
		g_task_return_new_error(task,
					FWUPD_ERROR,
					FWUPD_ERROR_AUTH_FAILED,
					"Failed to obtain auth as not trusted user");
		return;
	}

	/* success */
	g_task_return_boolean(task, TRUE);
}

static void
fu_polkit_authority_init(FuPolkitAuthority *self)
{
}

gboolean
fu_polkit_authority_load(FuPolkitAuthority *self, GError **error)
{
#ifdef HAVE_POLKIT
	self->pkauthority = polkit_authority_get_sync(NULL, error);
	if (self->pkauthority == NULL) {
		g_prefix_error(error, "failed to load authority: ");
		return FALSE;
	}
#endif
	return TRUE;
}

static void
fu_polkit_authority_finalize(GObject *obj)
{
#ifdef HAVE_POLKIT
	FuPolkitAuthority *self = FU_POLKIT_AUTHORITY(obj);
	g_object_unref(self->pkauthority);
#endif
	G_OBJECT_CLASS(fu_polkit_authority_parent_class)->finalize(obj);
}

static void
fu_polkit_authority_class_init(FuPolkitAuthorityClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS(klass);
	object_class->finalize = fu_polkit_authority_finalize;
}

FuPolkitAuthority *
fu_polkit_authority_new(void)
{
	FuPolkitAuthority *self;
	self = g_object_new(FU_TYPE_POLKIT_AUTHORITY, NULL);
	return FU_POLKIT_AUTHORITY(self);
}
