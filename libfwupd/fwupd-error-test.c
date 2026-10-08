/*
 * Copyright 2025 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include <gio/gio.h>

#include "fwupd-error.h"

static void
fwupd_error_string_func(void)
{
	/* round-trip every known error enum */
	for (guint i = 0; i < FWUPD_ERROR_LAST; i++) {
		const gchar *tmp = fwupd_error_to_string(i);
		g_assert_nonnull(tmp);
		g_assert_cmpint(fwupd_error_from_string(tmp), ==, i);
	}

	/* an out-of-range enum has no string */
	g_assert_null(fwupd_error_to_string(FWUPD_ERROR_LAST));

	/* an unknown string maps to the sentinel */
	g_assert_cmpint(fwupd_error_from_string("org.example.NotAnError"), ==, FWUPD_ERROR_LAST);
	g_assert_cmpint(fwupd_error_from_string(NULL), ==, FWUPD_ERROR_LAST);
}

static void
fwupd_error_convert_noop_func(void)
{
	g_autoptr(GError) error =
	    g_error_new_literal(FWUPD_ERROR, FWUPD_ERROR_NOT_SUPPORTED, "nope");

	/* a NULL error is a no-op */
	fwupd_error_convert(NULL);

	/* an error already in the FWUPD_ERROR domain is left alone */
	fwupd_error_convert(&error);
	g_assert_error(error, FWUPD_ERROR, FWUPD_ERROR_NOT_SUPPORTED);
}

static void
fwupd_error_convert_no_medium_func(void)
{
	g_autoptr(GError) error =
	    g_error_new_literal(G_IO_ERROR, G_IO_ERROR_FAILED, "No medium found");

	/* the "No medium found" special case */
	fwupd_error_convert(&error);
	g_assert_error(error, FWUPD_ERROR, FWUPD_ERROR_NOT_FOUND);
}

static void
fwupd_error_convert_mapped_func(void)
{
	g_autoptr(GError) error = g_error_new_literal(G_IO_ERROR, G_IO_ERROR_NOT_FOUND, "missing");

	/* a mapped GError is converted to the matching FwupdError */
	fwupd_error_convert(&error);
	g_assert_cmpint(error->domain, ==, FWUPD_ERROR);
}

int
main(int argc, char **argv)
{
	g_test_init(&argc, &argv, NULL);
	g_test_add_func("/fwupd/error/string", fwupd_error_string_func);
	g_test_add_func("/fwupd/error/convert/noop", fwupd_error_convert_noop_func);
	g_test_add_func("/fwupd/error/convert/no-medium", fwupd_error_convert_no_medium_func);
	g_test_add_func("/fwupd/error/convert/mapped", fwupd_error_convert_mapped_func);
	return g_test_run();
}
