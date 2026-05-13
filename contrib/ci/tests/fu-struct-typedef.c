/*
 * Copyright 2025 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 *
 * nocheck:expect: invalid struct name
 * nocheck:expect: incorrect struct name
 */

typedef struct {
	guint8 buf;
} FuStructPrefix;

typedef struct {
	guint16 buf;
} WrongPrefix;

typedef struct {
	gchar *id;
} FwupdBiosSettingPrivate;
