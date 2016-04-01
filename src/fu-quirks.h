/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2016 Mario Limonciello <mario_limonciello@dell.com>
 *
 * Licensed under the GNU General Public License Version 2
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#ifndef __FU_QUIRKS_H
#define __FU_QUIRKS_H

typedef struct {
	const gchar             *sys_vendor;
	const gchar		*identifier;
	AsVersionParseFlag       flags;
} FuVendorQuirks;

static const FuVendorQuirks quirk_table[] = {
	/* Dell & Alienware use AA.BB.CC.DD rather than AA.BB.CCDD */
	{ "Dell Inc.",	"com.dell.uefi",	AS_VERSION_PARSE_FLAG_NONE },
	{ "Alienware",	"com.dell.uefi",	AS_VERSION_PARSE_FLAG_NONE },
	{ NULL,		NULL,			AS_VERSION_PARSE_FLAG_NONE }
};

#endif /* __FU_QUIRKS_H */

