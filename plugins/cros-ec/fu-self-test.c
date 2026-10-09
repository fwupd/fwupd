/*
 * Copyright 2024 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include <fwupdplugin.h>

#include "fu-cros-ec-common.h"

static void
fu_cros_ec_common_version_func(void)
{
	g_autoptr(FuCrosEcVersion) version = NULL;
	g_autoptr(FuCrosEcVersion) version_dirty = NULL;
	g_autoptr(GError) error = NULL;

	/* clean version */
	version = fu_cros_ec_version_parse("cheese_v1.1.1755-4da9520", &error);
	g_assert_no_error(error);
	g_assert_nonnull(version);
	g_assert_cmpstr(version->boardname, ==, "cheese");
	g_assert_cmpstr(version->triplet, ==, "1.1.1755");
	g_assert_cmpstr(version->sha1, ==, "4da9520");
	g_assert_false(version->dirty);

	/* dirty version */
	version_dirty = fu_cros_ec_version_parse("cheese_v1.1.1755+4da9520", &error);
	g_assert_no_error(error);
	g_assert_nonnull(version_dirty);
	g_assert_true(version_dirty->dirty);
}

static void
fu_cros_ec_common_version_invalid_func(void)
{
	struct {
		const gchar *raw;
	} map[] = {
	    {""},
	    {"no-version-marker"},
	    {"cheese_v1.1.1755"},    /* no hash marker */
	    {"cheese_v1.1-4da9520"}, /* improper triplet */
	};
	for (guint i = 0; i < G_N_ELEMENTS(map); i++) {
		g_autoptr(FuCrosEcVersion) version = NULL;
		g_autoptr(GError) error = NULL;
		version = fu_cros_ec_version_parse(map[i].raw, &error);
		g_assert_error(error, FWUPD_ERROR, FWUPD_ERROR_INTERNAL);
		g_assert_null(version);
	}
}

int
main(int argc, char **argv)
{
	g_test_init(&argc, &argv, NULL);
	g_test_add_func("/cros-ec/common{version}", fu_cros_ec_common_version_func);
	g_test_add_func("/cros-ec/common{version-invalid}", fu_cros_ec_common_version_invalid_func);
	return g_test_run();
}
