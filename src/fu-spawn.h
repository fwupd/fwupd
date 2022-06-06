/*
 * Copyright (C) 2017 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#pragma once

#include <gio/gio.h>

/**
 * FuSpawnOutputHandler:
 * @line: text data
 * @user_data: user data
 *
 * The process spawn iteration callback.
 */
typedef void (*FuSpawnOutputHandler)(const gchar *line, gpointer user_data);

gboolean
fu_spawn_sync(const gchar *const *argv,
	      FuSpawnOutputHandler handler_cb,
	      gpointer handler_user_data,
	      guint timeout_ms,
	      GCancellable *cancellable,
	      GError **error) G_GNUC_WARN_UNUSED_RESULT;
