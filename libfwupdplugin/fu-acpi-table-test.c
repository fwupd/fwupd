/*
 * Copyright 2026 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include <fwupdplugin.h>

static void
fu_acpi_table_setters_func(void)
{
	g_autoptr(FuAcpiTable) table = FU_ACPI_TABLE(fu_acpi_table_new());

	/* defaults */
	g_assert_cmpint(fu_acpi_table_get_revision(table), ==, 0x0);
	g_assert_null(fu_acpi_table_get_oem_id(table));
	g_assert_null(fu_acpi_table_get_oem_table_id(table));
	g_assert_cmpint(fu_acpi_table_get_oem_revision(table), ==, 0x0);

	/* set then get */
	fu_acpi_table_set_revision(table, 0x42);
	g_assert_cmpint(fu_acpi_table_get_revision(table), ==, 0x42);

	fu_acpi_table_set_oem_id(table, "AMD");
	g_assert_cmpstr(fu_acpi_table_get_oem_id(table), ==, "AMD");

	fu_acpi_table_set_oem_table_id(table, "AmdFwCfg");
	g_assert_cmpstr(fu_acpi_table_get_oem_table_id(table), ==, "AmdFwCfg");

	fu_acpi_table_set_oem_revision(table, 0x12345678);
	g_assert_cmpint(fu_acpi_table_get_oem_revision(table), ==, 0x12345678);
}

static void
fu_acpi_table_setters_overwrite_func(void)
{
	g_autoptr(FuAcpiTable) table = FU_ACPI_TABLE(fu_acpi_table_new());

	/* string setters replace any previous value and do not leak */
	fu_acpi_table_set_oem_id(table, "FIRST");
	fu_acpi_table_set_oem_id(table, "SECOND");
	g_assert_cmpstr(fu_acpi_table_get_oem_id(table), ==, "SECOND");

	fu_acpi_table_set_oem_table_id(table, "TableOne");
	fu_acpi_table_set_oem_table_id(table, "TableTwo");
	g_assert_cmpstr(fu_acpi_table_get_oem_table_id(table), ==, "TableTwo");

	/* clearing back to NULL is allowed */
	fu_acpi_table_set_oem_id(table, NULL);
	g_assert_null(fu_acpi_table_get_oem_id(table));
	fu_acpi_table_set_oem_table_id(table, NULL);
	g_assert_null(fu_acpi_table_get_oem_table_id(table));
}

static void
fu_acpi_table_payload_func(void)
{
	gboolean ret;
	g_autoptr(FuAcpiTable) table = FU_ACPI_TABLE(fu_acpi_table_new());
	g_autoptr(FuInputStream) payload = NULL;
	g_autoptr(FuInputStream) payload_out = NULL;
	g_autoptr(GBytes) blob = g_bytes_new_static("hello", 5);
	g_autoptr(GBytes) blob_out = NULL;
	g_autoptr(GError) error = NULL;

	/* no payload by default */
	payload_out = fu_acpi_table_get_payload(table, &error);
	g_assert_error(error, FWUPD_ERROR, FWUPD_ERROR_INVALID_DATA);
	g_assert_null(payload_out);
	g_clear_error(&error);

	/* set then get */
	payload = fu_memory_input_stream_new_from_bytes(blob);
	fu_acpi_table_set_payload(table, payload);
	payload_out = fu_acpi_table_get_payload(table, &error);
	g_assert_no_error(error);
	g_assert_nonnull(payload_out);
	g_assert_true(payload_out == payload);

	/* the payload data round-trips */
	blob_out = fu_input_stream_read_bytes(payload_out, 0x0, G_MAXSIZE, NULL, &error);
	g_assert_no_error(error);
	g_assert_nonnull(blob_out);
	ret = fu_bytes_compare(blob, blob_out, &error);
	g_assert_no_error(error);
	g_assert_true(ret);

	/* clearing back to NULL is allowed */
	fu_acpi_table_set_payload(table, NULL);
	g_clear_object(&payload_out);
	payload_out = fu_acpi_table_get_payload(table, &error);
	g_assert_error(error, FWUPD_ERROR, FWUPD_ERROR_INVALID_DATA);
	g_assert_null(payload_out);
}

int
main(int argc, char **argv)
{
	g_test_init(&argc, &argv, NULL);
	g_test_add_func("/fwupd/acpi-table/setters", fu_acpi_table_setters_func);
	g_test_add_func("/fwupd/acpi-table/setters{overwrite}",
			fu_acpi_table_setters_overwrite_func);
	g_test_add_func("/fwupd/acpi-table/payload", fu_acpi_table_payload_func);
	return g_test_run();
}
