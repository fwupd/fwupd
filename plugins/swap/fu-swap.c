/*
 * Copyright (C) 2020 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include <string.h>

#include "fu-common.h"
#include "fu-swap.h"

struct _FuSwap {
	GObject		 parent_instance;
	gboolean	 encrypted;
	gboolean	 enabled;
};

G_DEFINE_TYPE (FuSwap, fu_swap, G_TYPE_OBJECT)

FuSwap *
fu_swap_new (const gchar *buf, gsize bufsz, GError **error)
{
	FuSwap *self = g_object_new (FU_TYPE_SWAP, NULL);
	g_auto(GStrv) lines = NULL;

	if (bufsz == 0)
		bufsz = strlen (buf);
	lines = fu_common_strnsplit (buf, bufsz, "\n", -1);
	if (g_strv_length (lines) > 2) {
		self->enabled = TRUE;
		for (guint i = 1; lines[i] != NULL && lines[i][0] != '\0'; i++) {
			if (g_str_has_prefix (lines[i], "/dev/dm-") ||
			    g_str_has_prefix (lines[i], "/dev/mapper")) {
				self->encrypted = TRUE;
				break;
			}
		}
	}
	return self;
}

gboolean
fu_swap_get_encrypted (FuSwap *self)
{
	g_return_val_if_fail (FU_IS_SWAP (self), FALSE);
	return self->encrypted;
}

gboolean
fu_swap_get_enabled (FuSwap *self)
{
	g_return_val_if_fail (FU_IS_SWAP (self), FALSE);
	return self->enabled;
}

static void
fu_swap_class_init (FuSwapClass *klass)
{
}

static void
fu_swap_init (FuSwap *self)
{
}
