/*
 * Copyright (C) 2017 Intel Corporation.
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include <string.h>
#include <glib.h>

#include "fu-thunderbolt-image.h"
#include "fu-plugin-vfuncs.h"
#include "fu-hash.h"

static gsize
read_farb_pointer (gchar *image)
{
	gsize ret = 0;
	memcpy (&ret, image, 3);
	ret = GSIZE_FROM_LE (ret);
	if (ret != 0 && ret != 0xFFFFFF)
		return ret;
	ret = 0;
	memcpy (&ret, image + 0x1000, 3);
	ret = GSIZE_FROM_LE (ret);
	g_assert (ret != 0 && ret != 0xFFFFFF);
	return ret;
}

int
main (int argc, char **argv)
{
	g_autoptr(GError) error = NULL;
	g_autoptr(GFile) fw_file = NULL;
	gboolean ok;
	gsize len;
	gchar *data = NULL;
	g_autoptr(GBytes) image = NULL;
	g_autoptr(GBytes) controller = NULL;
	FuPluginValidation validation;

	if (argc < 2 || argc > 3) {
		g_print ("Usage: %s <filename> [<controller>]\n", argv[0]);
		g_print ("Runs image validation on 'filename', comparing it to itself\n"
			 "after removing the headers or to 'controller' if given\n");
		return 1;
	}

	fw_file = g_file_new_for_path (argv[1]);
	g_assert_nonnull (fw_file);

	ok = g_file_load_contents (fw_file, NULL, &data, &len, NULL, &error);
	g_assert_no_error (error);
	g_assert_true (ok);

	image = g_bytes_new_take (data, len);

	if (argc == 2) {
		gssize header_size = read_farb_pointer (data);
		g_assert_cmpuint (header_size, !=, 0);
		g_assert_cmpuint (header_size, <, len);

		controller = g_bytes_new_from_bytes (image, header_size, len - header_size);
	} else {
		g_autoptr(GFile) controller_file = NULL;
		gsize controller_len;
		gchar *controller_data = NULL;

		controller_file = g_file_new_for_path (argv[2]);
		g_assert_nonnull (controller_file);

		ok = g_file_load_contents (controller_file,
					   NULL,
					   &controller_data,
					   &controller_len,
					   NULL,
					   &error);
		g_assert_no_error (error);
		g_assert_true (ok);

		controller = g_bytes_new_take (controller_data, controller_len);
	}

	validation = fu_thunderbolt_image_validate (controller, image, &error);
	g_assert_no_error (error);
	g_assert_cmpint (validation, ==, VALIDATION_PASSED);

	g_print ("test passed\n");
	return 0;
}
