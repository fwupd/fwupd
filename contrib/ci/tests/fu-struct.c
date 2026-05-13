/*
 * Copyright 2025 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 *
 * nocheck:expect: invalid struct name
 * nocheck:expect: incorrect struct name
 */

struct FuStructPrefix {
	guint8 buf;
}

struct WrongPrefix {
	guint8 buf;
}

const struct {
	guint8 buf;
} map[] = {
    {0x0},
};
