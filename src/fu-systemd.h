/*
 * Copyright (C) 2017 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#pragma once

#include <glib.h>

gboolean
fu_systemd_unit_stop(const gchar *unit, GError **error);
gchar *
fu_systemd_get_default_target(GError **error);
