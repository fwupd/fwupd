/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2015 Richard Hughes <richard@hughsie.com>
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

#include "config.h"

#include <fwupd.h>
#include <glib-object.h>
#include <string.h>

#include "fu-cleanup.h"
#include "fu-guid.h"

/**
 * fu_guid_is_valid:
 **/
gboolean
fu_guid_is_valid (const gchar *guid)
{
	_cleanup_strv_free_ gchar **split = NULL;
	if (guid == NULL)
		return FALSE;
	split = g_strsplit (guid, "-", -1);
	if (g_strv_length (split) != 5)
		return FALSE;
	if (strlen (split[0]) != 8)
		return FALSE;
	if (strlen (split[1]) != 4)
		return FALSE;
	if (strlen (split[2]) != 4)
		return FALSE;
	if (strlen (split[3]) != 4)
		return FALSE;
	if (strlen (split[4]) != 12)
		return FALSE;
	return TRUE;
}

/**
 * fu_guid_generate_from_string:
 **/
gchar *
fu_guid_generate_from_string (const gchar *str)
{
	gchar *tmp;
	tmp = g_compute_checksum_for_string (G_CHECKSUM_SHA1, str, -1);
	tmp[8] = '-';
	tmp[13] = '-';
	tmp[18] = '-';
	tmp[23] = '-';
	tmp[36] = '\0';
	g_assert (fu_guid_is_valid (tmp));
	return tmp;
}
