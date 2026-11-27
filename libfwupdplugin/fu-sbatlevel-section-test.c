/*
 * Copyright 2026 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include <fwupdplugin.h>

static GBytes *
fu_sbatlevel_section_test_build(const gchar *previous, const gchar *latest)
{
	g_autoptr(GByteArray) buf = g_byte_array_new();

	/* the offsets are relative to the end of the version field */
	fu_byte_array_append_uint32(buf, 0x0, G_LITTLE_ENDIAN);
	fu_byte_array_append_uint32(buf, sizeof(guint32) * 2, G_LITTLE_ENDIAN);
	fu_byte_array_append_uint32(buf,
				    (sizeof(guint32) * 2) + strlen(previous) + 1,
				    G_LITTLE_ENDIAN);
	g_byte_array_append(buf, (const guint8 *)previous, strlen(previous) + 1);
	g_byte_array_append(buf, (const guint8 *)latest, strlen(latest) + 1);
	return g_byte_array_free_to_bytes(g_steal_pointer(&buf));
}

static void
fu_sbatlevel_section_test_check_value(FuSbatlevelSection *self,
				      const gchar *section_id,
				      const gchar *entry_id,
				      const gchar *date_stamp)
{
	g_autoptr(FuFirmware) img_section = NULL;
	g_autoptr(FuFirmware) img_entry = NULL;
	g_autoptr(GError) error = NULL;

	img_section = fu_firmware_get_image_by_id(FU_FIRMWARE(self), section_id, &error);
	g_assert_no_error(error);
	g_assert_nonnull(img_section);
	img_entry = fu_firmware_get_image_by_id(img_section, entry_id, &error);
	g_assert_no_error(error);
	g_assert_nonnull(img_entry);
	g_assert_cmpstr(fu_csv_entry_get_value_by_column_id(FU_CSV_ENTRY(img_entry), "date_stamp"),
			==,
			date_stamp);
}

static void
fu_sbatlevel_section_parse_func(void)
{
	gboolean ret;
	g_autoptr(FuFirmware) firmware = fu_sbatlevel_section_new();
	g_autoptr(GBytes) blob = NULL;
	g_autoptr(GError) error = NULL;

	/* no trailing newline, so the byte before the NUL is part of the last value */
	blob = fu_sbatlevel_section_test_build("sbat,1,2021030218",
					       "sbat,1,2022052400\ngrub,2,2022052401");
	ret = fu_firmware_parse_bytes(firmware, blob, 0x0, FU_FIRMWARE_PARSE_FLAG_NONE, &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	fu_sbatlevel_section_test_check_value(FU_SBATLEVEL_SECTION(firmware),
					      "previous",
					      "sbat",
					      "2021030218");
	fu_sbatlevel_section_test_check_value(FU_SBATLEVEL_SECTION(firmware),
					      "latest",
					      "sbat",
					      "2022052400");
	fu_sbatlevel_section_test_check_value(FU_SBATLEVEL_SECTION(firmware),
					      "latest",
					      "grub",
					      "2022052401");
}

static void
fu_sbatlevel_section_empty_previous_func(void)
{
	gboolean ret;
	g_autoptr(FuFirmware) firmware = fu_sbatlevel_section_new();
	g_autoptr(GBytes) blob = NULL;
	g_autoptr(GError) error = NULL;

	/* a NUL at the very start must not be treated as the rest of the stream */
	blob = fu_sbatlevel_section_test_build("", "sbat,1,2022052400");
	ret = fu_firmware_parse_bytes(firmware, blob, 0x0, FU_FIRMWARE_PARSE_FLAG_NONE, &error);
	g_assert_error(error, FWUPD_ERROR, FWUPD_ERROR_NOT_SUPPORTED);
	g_assert_false(ret);
}

static void
fu_sbatlevel_section_empty_latest_func(void)
{
	gboolean ret;
	g_autoptr(FuFirmware) firmware = fu_sbatlevel_section_new();
	g_autoptr(GBytes) blob = NULL;
	g_autoptr(GError) error = NULL;

	/* a NUL at the very start must not be treated as the rest of the stream */
	blob = fu_sbatlevel_section_test_build("sbat,1,2021030218", "");
	ret = fu_firmware_parse_bytes(firmware, blob, 0x0, FU_FIRMWARE_PARSE_FLAG_NONE, &error);
	g_assert_error(error, FWUPD_ERROR, FWUPD_ERROR_NOT_SUPPORTED);
	g_assert_false(ret);
}

int
main(int argc, char **argv)
{
	g_test_init(&argc, &argv, NULL);
	g_test_add_func("/fwupd/sbatlevel-section/parse", fu_sbatlevel_section_parse_func);
	g_test_add_func("/fwupd/sbatlevel-section/empty-previous",
			fu_sbatlevel_section_empty_previous_func);
	g_test_add_func("/fwupd/sbatlevel-section/empty-latest",
			fu_sbatlevel_section_empty_latest_func);
	return g_test_run();
}
