/*
 * Copyright 2017 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include <fwupd.h>

/**
 * FuSpawnOutputHandler:
 * @line: text data
 * @user_data: (closure): user data
 *
 * The process spawn iteration callback.
 */
typedef void (*FuSpawnOutputHandler)(const gchar *line, gpointer user_data);

gboolean
fu_spawn_sync(const gchar *const *argv,
	      FuSpawnOutputHandler handler_cb,
	      gpointer user_data,
	      guint timeout_ms,
	      GCancellable *cancellable,
	      GError **error) G_GNUC_WARN_UNUSED_RESULT G_GNUC_NON_NULL(1);
