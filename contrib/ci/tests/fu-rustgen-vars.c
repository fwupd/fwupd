/*
 * Copyright 2025 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 *
 * nocheck:expect: rustgen structure 'buf' has to have 'st' prefix
 */

static gboolean
fu_rustgen_vars_test(void)
{
	g_autoptr(FuStructFixme) buf = fu_struct_fixme_new();
}
