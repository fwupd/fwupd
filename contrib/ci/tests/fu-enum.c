/*
 * Copyright 2025 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 *
 * nocheck:expect: invalid enum name
 */

enum FuBar {
	FOO,
}

enum WrongPrefix {
	FOO,
}

static void
foo_cb(enum flashrom_progress_stage stage, gboolean user_data)
{
}

enum { SIGNAL_CHANGED, SIGNAL_ADDED, SIGNAL_LAST };
