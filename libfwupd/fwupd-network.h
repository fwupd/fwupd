/*
 * Copyright 2026 Mario Limonciello <superm1@kernel.org>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include <glib.h>

gboolean
fwupd_network_is_reachable(const gchar *hostname, gint port, GError **error);
