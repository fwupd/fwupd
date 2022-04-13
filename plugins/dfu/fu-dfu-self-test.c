/*
 * Copyright (C) 2015 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include <fwupdplugin.h>

#include <string.h>

#include "fu-context-private.h"
#include "fu-dfu-device.h"
#include "fu-dfu-sector.h"
#include "fu-dfu-target-private.h"

static void
fu_dfu_enums_func(void)
{
	for (guint i = 0; i < FU_DFU_STATE_LAST; i++)
		g_assert_cmpstr(fu_dfu_state_to_string(i), !=, NULL);
	for (guint i = 0; i < FU_DFU_STATUS_LAST; i++)
		g_assert_cmpstr(fu_dfu_status_to_string(i), !=, NULL);
}

static gboolean
fu_test_compare_lines(const gchar *txt1, const gchar *txt2, GError **error)
{
	g_autofree gchar *output = NULL;
	if (g_strcmp0(txt1, txt2) == 0)
		return TRUE;
	if (fu_common_fnmatch(txt2, txt1))
		return TRUE;
	if (!g_file_set_contents("/tmp/a", txt1, -1, error))
		return FALSE;
	if (!g_file_set_contents("/tmp/b", txt2, -1, error))
		return FALSE;
	if (!g_spawn_command_line_sync("diff -urNp /tmp/b /tmp/a", &output, NULL, NULL, error))
		return FALSE;
	g_set_error_literal(error, 1, 0, output);
	return FALSE;
}

static gchar *
fu_dfu_target_sectors_to_string(FuDfuTarget *target)
{
	GPtrArray *sectors;
	GString *str;

	str = g_string_new("");
	sectors = fu_dfu_target_get_sectors(target);
	for (guint i = 0; i < sectors->len; i++) {
		FuDfuSector *sector = g_ptr_array_index(sectors, i);
		g_autofree gchar *tmp = fu_dfu_sector_to_string(sector);
		g_string_append_printf(str, "%s\n", tmp);
	}
	if (str->len > 0)
		g_string_truncate(str, str->len - 1);
	return g_string_free(str, FALSE);
}

static void
fu_dfu_target_dfuse_func(void)
{
	gboolean ret;
	gchar *tmp;
	g_autoptr(FuContext) ctx = fu_context_new();
	g_autoptr(FuDfuDevice) device = fu_dfu_device_new(ctx, NULL);
	g_autoptr(FuDfuTarget) target = NULL;
	g_autoptr(GError) error = NULL;

	/* NULL */
	target = g_object_new(FU_TYPE_DFU_TARGET, NULL);
	fu_dfu_target_set_device(target, device);
	ret = fu_dfu_target_parse_sectors(target, NULL, &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	tmp = fu_dfu_target_sectors_to_string(target);
	g_assert_cmpstr(tmp, ==, "");
	g_free(tmp);

	/* no addresses */
	ret = fu_dfu_target_parse_sectors(target, "@Flash3", &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	tmp = fu_dfu_target_sectors_to_string(target);
	g_assert_cmpstr(tmp, ==, "");
	g_free(tmp);

	/* one sector, no space */
	ret = fu_dfu_target_parse_sectors(target, "@Internal Flash /0x08000000/2*001Ka", &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	tmp = fu_dfu_target_sectors_to_string(target);
	ret = fu_test_compare_lines(tmp,
				    "Zone:0, Sec#:0, Addr:0x08000000, Size:0x0400, Caps:0x1 [R]\n"
				    "Zone:0, Sec#:0, Addr:0x08000400, Size:0x0400, Caps:0x1 [R]",
				    &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	g_free(tmp);

	/* multiple sectors */
	ret = fu_dfu_target_parse_sectors(target, "@Flash1   /0x08000000/2*001Ka,4*001Kg", &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	tmp = fu_dfu_target_sectors_to_string(target);
	ret = fu_test_compare_lines(tmp,
				    "Zone:0, Sec#:0, Addr:0x08000000, Size:0x0400, Caps:0x1 [R]\n"
				    "Zone:0, Sec#:0, Addr:0x08000400, Size:0x0400, Caps:0x1 [R]\n"
				    "Zone:0, Sec#:1, Addr:0x08000800, Size:0x0400, Caps:0x7 [REW]\n"
				    "Zone:0, Sec#:1, Addr:0x08000c00, Size:0x0400, Caps:0x7 [REW]\n"
				    "Zone:0, Sec#:1, Addr:0x08001000, Size:0x0400, Caps:0x7 [REW]\n"
				    "Zone:0, Sec#:1, Addr:0x08001400, Size:0x0400, Caps:0x7 [REW]",
				    &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	g_free(tmp);

	/* non-contiguous */
	ret = fu_dfu_target_parse_sectors(target,
					  "@Flash2 /0xF000/4*100Ba/0xE000/3*8Kg/0x80000/2*24Kg",
					  &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	tmp = fu_dfu_target_sectors_to_string(target);
	ret = fu_test_compare_lines(tmp,
				    "Zone:0, Sec#:0, Addr:0x0000f000, Size:0x0064, Caps:0x1 [R]\n"
				    "Zone:0, Sec#:0, Addr:0x0000f064, Size:0x0064, Caps:0x1 [R]\n"
				    "Zone:0, Sec#:0, Addr:0x0000f0c8, Size:0x0064, Caps:0x1 [R]\n"
				    "Zone:0, Sec#:0, Addr:0x0000f12c, Size:0x0064, Caps:0x1 [R]\n"
				    "Zone:1, Sec#:0, Addr:0x0000e000, Size:0x2000, Caps:0x7 [REW]\n"
				    "Zone:1, Sec#:0, Addr:0x00010000, Size:0x2000, Caps:0x7 [REW]\n"
				    "Zone:1, Sec#:0, Addr:0x00012000, Size:0x2000, Caps:0x7 [REW]\n"
				    "Zone:2, Sec#:0, Addr:0x00080000, Size:0x6000, Caps:0x7 [REW]\n"
				    "Zone:2, Sec#:0, Addr:0x00086000, Size:0x6000, Caps:0x7 [REW]",
				    &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	g_free(tmp);

	/* invalid */
	ret = fu_dfu_target_parse_sectors(target, "Flash", NULL);
	g_assert_true(ret);
	ret = fu_dfu_target_parse_sectors(target, "@Internal Flash /0x08000000", NULL);
	g_assert_false(ret);
	ret = fu_dfu_target_parse_sectors(target, "@Internal Flash /0x08000000/12*001a", NULL);
	g_assert_false(ret);
}

int
main(int argc, char **argv)
{
	g_test_init(&argc, &argv, NULL);

	/* only critical and error are fatal */
	g_log_set_fatal_mask(NULL, G_LOG_LEVEL_ERROR | G_LOG_LEVEL_CRITICAL);

	/* log everything */
	(void)g_setenv("G_MESSAGES_DEBUG", "all", FALSE);

	/* tests go here */
	g_test_add_func("/dfu/enums", fu_dfu_enums_func);
	g_test_add_func("/dfu/target(DfuSe}", fu_dfu_target_dfuse_func);
	return g_test_run();
}
