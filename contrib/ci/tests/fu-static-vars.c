/*
 * Copyright 2025 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 *
 * nocheck:expect: static variable FIXME not allowed
 */

static guint FIXME = TRUE;

static gboolean
fu_static_vars_test(void)
{
}
