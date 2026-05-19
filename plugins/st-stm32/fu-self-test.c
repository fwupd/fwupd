/*
 * Copyright 2026 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include "fu-context-private.h"
#include "fu-device-private.h"
#include "fu-st-stm32-common.h"
#include "fu-st-stm32-device.h"

static void
fu_st_stm32_cmd_base_func(void)
{
	/* mapped to a base command */
	g_assert_cmpint(fu_st_stm32_cmd_base(FU_ST_STM32_CMD_WRITE_MEMORY_NS),
			==,
			FU_ST_STM32_CMD_WRITE_MEMORY);
	g_assert_cmpint(fu_st_stm32_cmd_base(FU_ST_STM32_CMD_ERASE_EXTENDED),
			==,
			FU_ST_STM32_CMD_ERASE);
	g_assert_cmpint(fu_st_stm32_cmd_base(FU_ST_STM32_CMD_ERASE_EXTENDED_NS),
			==,
			FU_ST_STM32_CMD_ERASE);
	g_assert_cmpint(fu_st_stm32_cmd_base(FU_ST_STM32_CMD_WRITE_UNPROTECT_NS),
			==,
			FU_ST_STM32_CMD_WRITE_UNPROTECT);
	g_assert_cmpint(fu_st_stm32_cmd_base(FU_ST_STM32_CMD_WRITE_PROTECT_NS),
			==,
			FU_ST_STM32_CMD_WRITE_PROTECT);
	g_assert_cmpint(fu_st_stm32_cmd_base(FU_ST_STM32_CMD_READ_PROTECT_NS),
			==,
			FU_ST_STM32_CMD_READ_PROTECT);
	g_assert_cmpint(fu_st_stm32_cmd_base(FU_ST_STM32_CMD_READ_UNPROTECT_NS),
			==,
			FU_ST_STM32_CMD_READ_UNPROTECT);

	/* already a base command, or unknown */
	g_assert_cmpint(fu_st_stm32_cmd_base(FU_ST_STM32_CMD_GET), ==, FU_ST_STM32_CMD_GET);
	g_assert_cmpint(fu_st_stm32_cmd_base(FU_ST_STM32_CMD_ERASE), ==, FU_ST_STM32_CMD_ERASE);
	g_assert_cmpint(fu_st_stm32_cmd_base(FU_ST_STM32_CMD_INVALID), ==, FU_ST_STM32_CMD_INVALID);
}

static void
fu_st_stm32_crc_func(void)
{
	gboolean ret;
	guint32 crc = FU_ST_STM32_CRC_INIT_VALUE;
	guint8 buf1[] = {0x78, 0x56, 0x34, 0x12};
	guint8 buf2[] = {0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07};
	g_autoptr(GError) error = NULL;

	/* single word, matches the STM32 hardware CRC unit */
	ret = fu_st_stm32_crc(&crc, buf1, sizeof(buf1), &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	g_assert_cmpint(crc, ==, 0xDF8A8A2B);

	/* multiple words */
	crc = FU_ST_STM32_CRC_INIT_VALUE;
	ret = fu_st_stm32_crc(&crc, buf2, sizeof(buf2), &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	g_assert_cmpint(crc, ==, 0x9C08808D);

	/* incremental is the same as all-at-once */
	crc = FU_ST_STM32_CRC_INIT_VALUE;
	ret = fu_st_stm32_crc(&crc, buf2, 4, &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	ret = fu_st_stm32_crc(&crc, buf2 + 4, 4, &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	g_assert_cmpint(crc, ==, 0x9C08808D);

	/* empty buffer does not change the value */
	crc = FU_ST_STM32_CRC_INIT_VALUE;
	ret = fu_st_stm32_crc(&crc, buf2, 0, &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	g_assert_cmpint(crc, ==, FU_ST_STM32_CRC_INIT_VALUE);

	/* not a multiple of 4, and value is not modified */
	ret = fu_st_stm32_crc(&crc, buf2, 3, &error);
	g_assert_error(error, FWUPD_ERROR, FWUPD_ERROR_INVALID_DATA);
	g_assert_false(ret);
	g_assert_cmpint(crc, ==, FU_ST_STM32_CRC_INIT_VALUE);
}

static void
fu_st_stm32_device_quirks_func(void)
{
	gboolean ret;
	g_autofree gchar *str = NULL;
	g_auto(GStrv) lines = NULL;
	g_autoptr(FuContext) ctx = fu_context_new();
	g_autoptr(FuDevice) device = g_object_new(FU_TYPE_ST_STM32_DEVICE, "context", ctx, NULL);
	g_autoptr(GError) error = NULL;
	struct {
		const gchar *key;
		const gchar *value;
		const gchar *title;
		const gchar *expected;
	} quirks[] = {
	    {"StStm32SramAddr", "0x20001000", "SramAddr", "0x20001000"},
	    {"StStm32SramLen", "0x2000", "SramLen", "0x2000"},
	    {"StStm32FlashAddr", "0x8000000", "FlashAddr", "0x8000000"},
	    {"StStm32FlashLen", "0x8000", "FlashLen", "0x8000"},
	    {"StStm32PagesPerSector", "4", "PagesPerSector", "0x4"},
	    {"StStm32PageSize", "0x800", "PageSize", "0x800"},
	    {"StStm32OptionAddr", "0x1fff7800", "OptionAddr", "0x1fff7800"},
	    {"StStm32OptionLen", "0x7f", "OptionLen", "0x7f"},
	    {"StStm32MemAddr", "0x1fff0000", "MemAddr", "0x1fff0000"},
	    {"StStm32MemLen", "0x1800", "MemLen", "0x1800"},
	};

	/* all the plugin-specific keys */
	for (guint i = 0; i < G_N_ELEMENTS(quirks); i++) {
		ret = fu_device_set_quirk_kv(device,
					     quirks[i].key,
					     quirks[i].value,
					     FU_CONTEXT_QUIRK_SOURCE_DB,
					     &error);
		g_assert_no_error(error);
		g_assert_true(ret);
	}
	str = fu_device_to_string(device);
	g_debug("%s", str);
	lines = g_strsplit(str, "\n", -1);
	for (guint i = 0; i < G_N_ELEMENTS(quirks); i++) {
		g_autofree gchar *title = g_strdup_printf("%s:", quirks[i].title);
		gboolean found = FALSE;
		for (guint j = 0; lines[j] != NULL; j++) {
			g_auto(GStrv) tokens = g_strsplit_set(g_strstrip(lines[j]), " ", -1);
			if (g_strcmp0(tokens[0], title) != 0)
				continue;
			g_assert_cmpstr(tokens[g_strv_length(tokens) - 1], ==, quirks[i].expected);
			found = TRUE;
		}
		g_assert_true(found);
	}

	/* defaults from init */
	g_assert_true(fu_device_has_protocol(device, "com.st.stm32"));
	g_assert_true(fu_device_has_flag(device, FWUPD_DEVICE_FLAG_UPDATABLE));
	g_assert_true(fu_device_has_flag(device, FWUPD_DEVICE_FLAG_UNSIGNED_PAYLOAD));
	g_assert_cmpint(fu_device_get_version_format(device), ==, FWUPD_VERSION_FORMAT_TRIPLET);
}

static void
fu_st_stm32_device_quirks_invalid_func(void)
{
	gboolean ret;
	g_autoptr(FuContext) ctx = fu_context_new();
	g_autoptr(FuDevice) device = g_object_new(FU_TYPE_ST_STM32_DEVICE, "context", ctx, NULL);
	const gchar *keys[] = {
	    "StStm32SramAddr",
	    "StStm32SramLen",
	    "StStm32FlashAddr",
	    "StStm32FlashLen",
	    "StStm32PagesPerSector",
	    "StStm32PageSize",
	    "StStm32OptionAddr",
	    "StStm32OptionLen",
	    "StStm32MemAddr",
	    "StStm32MemLen",
	};

	/* not a number */
	for (guint i = 0; i < G_N_ELEMENTS(keys); i++) {
		g_autoptr(GError) error = NULL;
		ret = fu_device_set_quirk_kv(device,
					     keys[i],
					     "dave",
					     FU_CONTEXT_QUIRK_SOURCE_DB,
					     &error);
		g_assert_error(error, FWUPD_ERROR, FWUPD_ERROR_INVALID_DATA);
		g_assert_false(ret);
	}

	/* larger than 32 bits */
	for (guint i = 0; i < G_N_ELEMENTS(keys); i++) {
		g_autoptr(GError) error = NULL;
		ret = fu_device_set_quirk_kv(device,
					     keys[i],
					     "0x100000000",
					     FU_CONTEXT_QUIRK_SOURCE_DB,
					     &error);
		g_assert_error(error, FWUPD_ERROR, FWUPD_ERROR_INVALID_DATA);
		g_assert_false(ret);
	}
}

static void
fu_st_stm32_device_private_flags_func(void)
{
	g_autoptr(FuContext) ctx = fu_context_new();
	g_autoptr(FuDevice) device = g_object_new(FU_TYPE_ST_STM32_DEVICE, "context", ctx, NULL);
	const gchar *flags[] = {
	    "no-mass-erase",
	    "clear-pempty",
	    "obl-launch-required",
	};

	/* all registered so the quirk file can use them */
	for (guint i = 0; i < G_N_ELEMENTS(flags); i++) {
		fu_device_add_private_flag(device, flags[i]);
		g_assert_true(fu_device_has_private_flag(device, flags[i]));
	}
}

int
main(int argc, char **argv)
{
	(void)g_setenv("G_TEST_SRCDIR", SRCDIR, FALSE);
	g_test_init(&argc, &argv, NULL);
	g_test_add_func("/st-stm32/cmd-base", fu_st_stm32_cmd_base_func);
	g_test_add_func("/st-stm32/crc", fu_st_stm32_crc_func);
	g_test_add_func("/st-stm32/device/quirks", fu_st_stm32_device_quirks_func);
	g_test_add_func("/st-stm32/device/quirks-invalid", fu_st_stm32_device_quirks_invalid_func);
	g_test_add_func("/st-stm32/device/private-flags", fu_st_stm32_device_private_flags_func);
	return g_test_run();
}
