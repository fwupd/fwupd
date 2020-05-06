/*
 * Copyright (C) 2020 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#pragma once

#include <gio/gio.h>

#define FU_TYPE_SWAP (fu_swap_get_type ())
G_DECLARE_FINAL_TYPE (FuSwap, fu_swap, FU, SWAP, GObject)

FuSwap		*fu_swap_new			(const gchar	*buf,
						 gsize		 bufsz,
						 GError		**error);
gboolean	 fu_swap_get_enabled		(FuSwap		*self);
gboolean	 fu_swap_get_encrypted		(FuSwap		*self);
