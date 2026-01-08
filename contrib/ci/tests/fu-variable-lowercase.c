/*
 * Copyright 2025 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 *
 * nocheck:expect: mixed case variable
 * nocheck:expect: mixed case struct member
 * nocheck:expect: do not use param guint8 buf[]
 */

typedef struct {
	guint8 this_is_fine;
	guint8 Enter_SBL;
} FuTest;

static void
fu_variable_lowercase(guint8 buf[], gpointer user_data)
{
	guint16 Enter_SBL = 0;
	guint8 *PRAM = NULL;
}
