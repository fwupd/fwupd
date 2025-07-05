/*
 * Copyright 2020 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include <gio/gio.h>

#define FU_TYPE_LINUX_SWAP (fu_linux_swap_get_type())
G_DECLARE_FINAL_TYPE(FuLinuxSwap, fu_linux_swap, FU, LINUX_SWAP, GObject)

FuLinuxSwap *
fu_linux_swap_new(const gchar *buf, gsize bufsz, GError **error);
gboolean
fu_linux_swap_get_enabled(FuLinuxSwap *self);
gboolean
fu_linux_swap_get_encrypted(FuLinuxSwap *self);
