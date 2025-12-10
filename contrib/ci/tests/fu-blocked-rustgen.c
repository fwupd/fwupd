/*
 * Copyright 2025 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 *
 * nocheck:expect: use rustgen instead
 */

typedef struct __attribute__((packed)) {
	guint8 data;
} Foo;

typedef struct __attribute__((packed)) { /* nocheck:blocked */
	guint8 e[6];
} fwupd_guid_native_t;
