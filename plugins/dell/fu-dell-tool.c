/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2017 Mario Limonciello <mario_limonciello@dell.com>
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

#include "fu-dell-common.h"
#include <glib/gstdio.h>

static void usage(void) {
	g_print ("This application forces TBT/MST controllers to flashing mode on Dell systems\n\n");
	g_print ("Call with an argument of '1' to force flashing mode\n");
	g_print ("Call with an argument of '0' to return to normal mode\n");
}

int main(int argc, char *argv[]) {
	int ret;
	int mode;
	if (argc == 2) {
		if (strcmp(argv[1], "1") == 0)
			mode = DACI_FLASH_MODE_FLASH;
		else if (strcmp(argv[1], "0") == 0)
			mode = DACI_FLASH_MODE_USER;
		else {
			usage();
			return -1;
		}
		ret = fu_dell_toggle_flash(NULL, NULL, mode);
		if (!ret) {
			g_print("Failed to set device to %d (ret %d)\n", mode, ret);
			return -1;
		}
		g_print("Turned device to %d\n", mode);
	}
	else
		usage();
	return 0;
}
