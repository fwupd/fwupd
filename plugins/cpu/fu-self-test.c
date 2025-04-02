/*
 * Copyright 2025 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include "fu-cpu-common.h"

static void
fu_cpu_amd_entry_sign(void)
{
	const gchar *agesa_version;
	guint32 ucode_version;

	ucode_version = fu_cpu_amd_model_id_to_entry_sign_fixed_ucode_version(0x00AA0F02);
	g_assert_cmpint(ucode_version, ==, 0x0AA00219);

	agesa_version = fu_cpu_amd_stream_name_to_entry_sign_fixed_agesa_version("EmbeddedPI-FP5");
	g_assert_cmpstr(agesa_version, ==, "1.2.0.F");
	g_assert_cmpint(fu_version_compare("1.2.0.0", agesa_version, FWUPD_VERSION_FORMAT_UNKNOWN),
			<,
			0);
	g_assert_cmpint(fu_version_compare("1.2.0.A", agesa_version, FWUPD_VERSION_FORMAT_UNKNOWN),
			<,
			0);
	g_assert_cmpint(fu_version_compare("1.2.1.0", agesa_version, FWUPD_VERSION_FORMAT_UNKNOWN),
			>,
			0);
	g_assert_cmpint(fu_version_compare("1.2.0.F", agesa_version, FWUPD_VERSION_FORMAT_UNKNOWN),
			==,
			0);
}

int
main(int argc, char **argv)
{
	(void)g_setenv("G_MESSAGES_DEBUG", "all", TRUE);
	g_test_init(&argc, &argv, NULL);
	g_test_add_func("/uefi/cpu{amd-entry-sign}", fu_cpu_amd_entry_sign);
	return g_test_run();
}
