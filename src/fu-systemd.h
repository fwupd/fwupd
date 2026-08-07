/*
 * Copyright 2017 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include <fwupd.h>

G_BEGIN_DECLS

gboolean
fu_systemd_unit_stop(const gchar *unit, GError **error) G_GNUC_NON_NULL(1);

G_END_DECLS
