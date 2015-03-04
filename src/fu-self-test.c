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

#include <glib-object.h>
#include <glib/gstdio.h>

#include "fu-cleanup.h"
#include "fu-common.h"
#include "fu-pending.h"

static void
fu_pending_func (void)
{
	GError *error = NULL;
	gboolean ret;
	FuDevice *device;
	_cleanup_object_unref_ FuPending *pending = NULL;
	_cleanup_free_ gchar *dirname = NULL;
	_cleanup_free_ gchar *filename = NULL;

	/* create */
	pending = fu_pending_new ();
	g_assert (pending != NULL);

	/* delete the database */
	dirname = g_build_filename (LOCALSTATEDIR, "lib", "fwupd", NULL);
	if (!g_file_test (dirname, G_FILE_TEST_IS_DIR))
		return;
	filename = g_build_filename (dirname, "pending.db", NULL);
	g_unlink (filename);

	/* add a device */
	device = fu_device_new ();
	fu_device_set_id (device, "self-test");
	fu_device_set_metadata (device, FU_DEVICE_KEY_FILENAME_CAB, "/var/lib/dave.cap"),
	fu_device_set_metadata (device, FU_DEVICE_KEY_DISPLAY_NAME, "ColorHug"),
	fu_device_set_metadata (device, FU_DEVICE_KEY_VERSION, "3.0.1"),
	fu_device_set_metadata (device, FU_DEVICE_KEY_VERSION_NEW, "3.0.2");
	ret = fu_pending_add_device (pending, device, &error);
	g_assert_no_error (error);
	g_assert (ret);
	g_object_unref (device);

	/* ensure database was created */
	g_assert (g_file_test (filename, G_FILE_TEST_EXISTS));

	/* get device */
	device = fu_pending_get_device (pending, "self-test", &error);
	g_assert_no_error (error);
	g_assert (device != NULL);
	g_assert_cmpstr (fu_device_get_id (device), ==, "self-test");
	g_assert_cmpstr (fu_device_get_metadata (device, FU_DEVICE_KEY_FILENAME_CAB), ==, "/var/lib/dave.cap");
	g_assert_cmpstr (fu_device_get_metadata (device, FU_DEVICE_KEY_DISPLAY_NAME), ==, "ColorHug");
	g_assert_cmpstr (fu_device_get_metadata (device, FU_DEVICE_KEY_VERSION), ==, "3.0.1");
	g_assert_cmpstr (fu_device_get_metadata (device, FU_DEVICE_KEY_VERSION_NEW), ==, "3.0.2");
	g_object_unref (device);

	/* get device that does not exist */
	device = fu_pending_get_device (pending, "XXXXXXXXXXXXX", &error);
	g_assert_error (error, FU_ERROR, FU_ERROR_INTERNAL);
	g_assert (device == NULL);
	g_clear_error (&error);

	/* remove device */
	device = fu_device_new ();
	fu_device_set_id (device, "self-test");
	ret = fu_pending_remove_device (pending, device, &error);
	g_assert_no_error (error);
	g_assert (ret);
	g_object_unref (device);

	/* get device that does not exist */
	device = fu_pending_get_device (pending, "self-test", &error);
	g_assert_error (error, FU_ERROR, FU_ERROR_INTERNAL);
	g_assert (device == NULL);
	g_clear_error (&error);
}

int
main (int argc, char **argv)
{
	g_test_init (&argc, &argv, NULL);

	/* only critical and error are fatal */
	g_log_set_fatal_mask (NULL, G_LOG_LEVEL_ERROR | G_LOG_LEVEL_CRITICAL);

	/* tests go here */
	g_test_add_func ("/fwupd/pending", fu_pending_func);
	return g_test_run ();
}

