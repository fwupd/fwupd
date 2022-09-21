/*
 * Copyright (C) 2015 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include <fwupdplugin.h>

#include <glib/gstdio.h>
#include <libgcab.h>
#include <string.h>

#include "fwupd-bios-setting-private.h"
#include "fwupd-security-attr-private.h"

#include "fu-bios-settings-private.h"
#include "fu-cabinet.h"
#include "fu-common-private.h"
#include "fu-context-private.h"
#include "fu-coswid-firmware.h"
#include "fu-device-private.h"
#include "fu-plugin-private.h"
#include "fu-security-attrs-private.h"
#include "fu-smbios-private.h"

static GMainLoop *_test_loop = NULL;
static guint _test_loop_timeout_id = 0;

static gboolean
fu_test_hang_check_cb(gpointer user_data)
{
	g_main_loop_quit(_test_loop);
	_test_loop_timeout_id = 0;
	return G_SOURCE_REMOVE;
}

static void
fu_test_loop_run_with_timeout(guint timeout_ms)
{
	g_assert_cmpint(_test_loop_timeout_id, ==, 0);
	g_assert_null(_test_loop);
	_test_loop = g_main_loop_new(NULL, FALSE);
	_test_loop_timeout_id = g_timeout_add(timeout_ms, fu_test_hang_check_cb, NULL);
	g_main_loop_run(_test_loop);
}

static void
fu_test_loop_quit(void)
{
	if (_test_loop_timeout_id > 0) {
		g_source_remove(_test_loop_timeout_id);
		_test_loop_timeout_id = 0;
	}
	if (_test_loop != NULL) {
		g_main_loop_quit(_test_loop);
		g_main_loop_unref(_test_loop);
		_test_loop = NULL;
	}
}

static void
fu_archive_invalid_func(void)
{
	g_autofree gchar *filename = NULL;
	g_autoptr(FuArchive) archive = NULL;
	g_autoptr(GBytes) data = NULL;
	g_autoptr(GError) error = NULL;

#ifndef HAVE_LIBARCHIVE
	g_test_skip("no libarchive support");
	return;
#endif

	filename = g_test_build_filename(G_TEST_DIST, "tests", "metadata.xml", NULL);
	data = fu_bytes_get_contents(filename, &error);
	g_assert_no_error(error);
	g_assert_nonnull(data);

	archive = fu_archive_new(data, FU_ARCHIVE_FLAG_NONE, &error);
	g_assert_error(error, G_IO_ERROR, G_IO_ERROR_NOT_SUPPORTED);
	g_assert_null(archive);
}

static void
fu_archive_cab_func(void)
{
	g_autofree gchar *checksum1 = NULL;
	g_autofree gchar *checksum2 = NULL;
	g_autofree gchar *filename = NULL;
	g_autoptr(FuArchive) archive = NULL;
	g_autoptr(GBytes) data = NULL;
	g_autoptr(GError) error = NULL;
	GBytes *data_tmp;

#ifndef HAVE_LIBARCHIVE
	g_test_skip("no libarchive support");
	return;
#endif

	filename = g_test_build_filename(G_TEST_BUILT,
					 "tests",
					 "colorhug",
					 "colorhug-als-3.0.2.cab",
					 NULL);
	data = fu_bytes_get_contents(filename, &error);
	g_assert_no_error(error);
	g_assert_nonnull(data);

	archive = fu_archive_new(data, FU_ARCHIVE_FLAG_NONE, &error);
	g_assert_no_error(error);
	g_assert_nonnull(archive);

	data_tmp = fu_archive_lookup_by_fn(archive, "firmware.metainfo.xml", &error);
	g_assert_no_error(error);
	g_assert_nonnull(data_tmp);
	checksum1 = g_compute_checksum_for_bytes(G_CHECKSUM_SHA1, data_tmp);
	g_assert_cmpstr(checksum1, ==, "8611114f51f7151f190de86a5c9259d79ff34216");

	data_tmp = fu_archive_lookup_by_fn(archive, "firmware.bin", &error);
	g_assert_no_error(error);
	g_assert_nonnull(data_tmp);
	checksum2 = g_compute_checksum_for_bytes(G_CHECKSUM_SHA1, data_tmp);
	g_assert_cmpstr(checksum2, ==, "7c0ae84b191822bcadbdcbe2f74a011695d783c7");

	data_tmp = fu_archive_lookup_by_fn(archive, "NOTGOINGTOEXIST.xml", &error);
	g_assert_error(error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND);
	g_assert_null(data_tmp);
}

static void
fu_common_gpt_type_func(void)
{
	g_assert_cmpstr(fu_common_convert_to_gpt_type("0xef"),
			==,
			"c12a7328-f81f-11d2-ba4b-00a0c93ec93b");
	g_assert_cmpstr(fu_common_convert_to_gpt_type("0x0b"),
			==,
			"ebd0a0a2-b9e5-4433-87c0-68b6b72699c7");
	g_assert_cmpstr(fu_common_convert_to_gpt_type("fat32lba"),
			==,
			"ebd0a0a2-b9e5-4433-87c0-68b6b72699c7");
	g_assert_cmpstr(fu_common_convert_to_gpt_type("0x00"), ==, "0x00");
}

static void
fu_common_align_up_func(void)
{
	g_assert_cmpint(fu_common_align_up(0, 0), ==, 0);
	g_assert_cmpint(fu_common_align_up(5, 0), ==, 5);
	g_assert_cmpint(fu_common_align_up(5, 3), ==, 8);
	g_assert_cmpint(fu_common_align_up(1023, 10), ==, 1024);
	g_assert_cmpint(fu_common_align_up(1024, 10), ==, 1024);
	g_assert_cmpint(fu_common_align_up(G_MAXSIZE - 1, 10), ==, G_MAXSIZE);
}

static void
fu_common_byte_array_func(void)
{
	g_autoptr(GByteArray) array = g_byte_array_new();

	fu_byte_array_append_uint8(array, (guint8)'h');
	fu_byte_array_append_uint8(array, (guint8)'e');
	fu_byte_array_append_uint8(array, (guint8)'l');
	fu_byte_array_append_uint8(array, (guint8)'l');
	fu_byte_array_append_uint8(array, (guint8)'o');
	g_assert_cmpint(array->len, ==, 5);
	g_assert_cmpint(memcmp(array->data, "hello", array->len), ==, 0);

	fu_byte_array_set_size(array, 10, 0x00);
	g_assert_cmpint(array->len, ==, 10);
	g_assert_cmpint(memcmp(array->data, "hello\0\0\0\0\0", array->len), ==, 0);
}

static void
fu_common_crc_func(void)
{
	guint8 buf[] = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09};
	g_assert_cmpint(fu_crc8(buf, sizeof(buf)), ==, 0x7A);
	g_assert_cmpint(fu_crc16(buf, sizeof(buf)), ==, 0x4DF1);
	g_assert_cmpint(fu_crc32(buf, sizeof(buf)), ==, 0x40EFAB9E);
}

static void
fu_string_append_func(void)
{
	g_autoptr(GString) str = g_string_new(NULL);
	fu_string_append(str, 0, "hdr", NULL);
	fu_string_append(str, 0, "key", "value");
	fu_string_append(str, 0, "key1", "value1");
	fu_string_append(str, 1, "key2", "value2");
	fu_string_append(str, 1, "", "value2");
	fu_string_append(str, 2, "key3", "value3");
	g_assert_cmpstr(str->str,
			==,
			"hdr:\n"
			"key:                    value\n"
			"key1:                   value1\n"
			"  key2:                 value2\n"
			"                        value2\n"
			"    key3:               value3\n");
}

static void
fu_version_guess_format_func(void)
{
	g_assert_cmpint(fu_version_guess_format(NULL), ==, FWUPD_VERSION_FORMAT_UNKNOWN);
	g_assert_cmpint(fu_version_guess_format(""), ==, FWUPD_VERSION_FORMAT_UNKNOWN);
	g_assert_cmpint(fu_version_guess_format("1234ac"), ==, FWUPD_VERSION_FORMAT_PLAIN);
	g_assert_cmpint(fu_version_guess_format("1.2"), ==, FWUPD_VERSION_FORMAT_PAIR);
	g_assert_cmpint(fu_version_guess_format("1.2.3"), ==, FWUPD_VERSION_FORMAT_TRIPLET);
	g_assert_cmpint(fu_version_guess_format("1.2.3.4"), ==, FWUPD_VERSION_FORMAT_QUAD);
	g_assert_cmpint(fu_version_guess_format("1.2.3.4.5"), ==, FWUPD_VERSION_FORMAT_UNKNOWN);
	g_assert_cmpint(fu_version_guess_format("1a.2b.3"), ==, FWUPD_VERSION_FORMAT_PLAIN);
	g_assert_cmpint(fu_version_guess_format("1"), ==, FWUPD_VERSION_FORMAT_NUMBER);
	g_assert_cmpint(fu_version_guess_format("0x10201"), ==, FWUPD_VERSION_FORMAT_NUMBER);
}

static void
fu_device_version_format_func(void)
{
	g_autoptr(FuDevice) device = fu_device_new(NULL);
	fu_device_add_internal_flag(device, FU_DEVICE_INTERNAL_FLAG_ENSURE_SEMVER);
	fu_device_set_version_format(device, FWUPD_VERSION_FORMAT_TRIPLET);
	fu_device_set_version(device, "Ver1.2.3 RELEASE");
	g_assert_cmpstr(fu_device_get_version(device), ==, "1.2.3");
}

static void
fu_device_open_refcount_func(void)
{
	gboolean ret;
	g_autoptr(FuDevice) device = fu_device_new(NULL);
	g_autoptr(GError) error = NULL;
	fu_device_set_id(device, "test_device");
	ret = fu_device_open(device, &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	ret = fu_device_open(device, &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	ret = fu_device_close(device, &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	ret = fu_device_close(device, &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	ret = fu_device_close(device, &error);
	g_assert_error(error, FWUPD_ERROR, FWUPD_ERROR_NOTHING_TO_DO);
	g_assert_false(ret);
}

static void
fu_device_name_func(void)
{
	g_autoptr(FuDevice) device1 = fu_device_new(NULL);
	g_autoptr(FuDevice) device2 = fu_device_new(NULL);

	/* vendor then name */
	fu_device_set_vendor(device1, "  Hughski  ");
	fu_device_set_name(device1, "HUGHSKI  ColorHug(TM)__Pro  ");
	g_assert_cmpstr(fu_device_get_vendor(device1), ==, "Hughski");
	g_assert_cmpstr(fu_device_get_name(device1), ==, "ColorHug™ Pro");

	/* name then vendor */
	fu_device_set_name(device2, "Hughski ColorHug(TM)_Pro");
	fu_device_set_vendor(device2, "Hughski");
	g_assert_cmpstr(fu_device_get_vendor(device2), ==, "Hughski");
	g_assert_cmpstr(fu_device_get_name(device2), ==, "ColorHug™ Pro");

	/* a real example */
	fu_device_set_name(device2, "Intel(R) Core(TM) i7-10850H CPU @ 2.70GHz");
	fu_device_set_vendor(device2, "Intel");
	g_assert_cmpstr(fu_device_get_name(device2), ==, "Core™ i7-10850H CPU @ 2.70GHz");
}

static void
fu_device_cfi_device_func(void)
{
	gboolean ret;
	guint8 cmd = 0;
	g_autoptr(FuContext) ctx = fu_context_new();
	g_autoptr(FuCfiDevice) cfi_device = NULL;
	g_autoptr(GError) error = NULL;

	ret = fu_context_load_quirks(ctx, FU_QUIRKS_LOAD_FLAG_NO_CACHE, &error);
	g_assert_no_error(error);
	g_assert_true(ret);

	cfi_device = fu_cfi_device_new(ctx, "3730");
	ret = fu_device_setup(FU_DEVICE(cfi_device), &error);
	g_assert_no_error(error);
	g_assert_true(ret);

	/* fallback */
	ret = fu_cfi_device_get_cmd(cfi_device, FU_CFI_DEVICE_CMD_READ_DATA, &cmd, &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	g_assert_cmpint(cmd, ==, 0x03);

	/* from quirk */
	ret = fu_cfi_device_get_cmd(cfi_device, FU_CFI_DEVICE_CMD_CHIP_ERASE, &cmd, &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	g_assert_cmpint(cmd, ==, 0xC7);
	g_assert_cmpint(fu_cfi_device_get_size(cfi_device), ==, 0x10000);
	g_assert_cmpint(fu_cfi_device_get_page_size(cfi_device), ==, 0x200);
	g_assert_cmpint(fu_cfi_device_get_sector_size(cfi_device), ==, 0x2000);
	g_assert_cmpint(fu_cfi_device_get_block_size(cfi_device), ==, 0x8000);
}

static void
fu_device_metadata_func(void)
{
	g_autoptr(FuDevice) device = fu_device_new(NULL);

	/* string */
	fu_device_set_metadata(device, "foo", "bar");
	g_assert_cmpstr(fu_device_get_metadata(device, "foo"), ==, "bar");
	fu_device_set_metadata(device, "foo", "baz");
	g_assert_cmpstr(fu_device_get_metadata(device, "foo"), ==, "baz");
	g_assert_null(fu_device_get_metadata(device, "unknown"));

	/* boolean */
	fu_device_set_metadata_boolean(device, "baz", TRUE);
	g_assert_cmpstr(fu_device_get_metadata(device, "baz"), ==, "true");
	g_assert_true(fu_device_get_metadata_boolean(device, "baz"));
	g_assert_false(fu_device_get_metadata_boolean(device, "unknown"));

	/* integer */
	fu_device_set_metadata_integer(device, "bam", 12345);
	g_assert_cmpstr(fu_device_get_metadata(device, "bam"), ==, "12345");
	g_assert_cmpint(fu_device_get_metadata_integer(device, "bam"), ==, 12345);
	g_assert_cmpint(fu_device_get_metadata_integer(device, "unknown"), ==, G_MAXUINT);

	/* broken integer */
	fu_device_set_metadata(device, "bam", "123junk");
	g_assert_cmpint(fu_device_get_metadata_integer(device, "bam"), ==, G_MAXUINT);
	fu_device_set_metadata(device, "huge", "4294967296"); /* not 32 bit */
	g_assert_cmpint(fu_device_get_metadata_integer(device, "huge"), ==, G_MAXUINT);
}

static void
fu_smbios_func(void)
{
	const gchar *str;
	gboolean ret;
	g_autofree gchar *dump = NULL;
	g_autofree gchar *testdatadir = NULL;
	g_autoptr(FuSmbios) smbios = NULL;
	g_autoptr(GError) error = NULL;

#ifdef _WIN32
	g_test_skip("Windows uses GetSystemFirmwareTable rather than parsing the fake test data");
	return;
#endif

	/* these tests will not write */
	testdatadir = g_test_build_filename(G_TEST_DIST, "tests", NULL);
	(void)g_setenv("FWUPD_SYSFSFWDIR", testdatadir, TRUE);

	smbios = fu_smbios_new();
	ret = fu_smbios_setup(smbios, &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	dump = fu_firmware_to_string(FU_FIRMWARE(smbios));
	if (g_getenv("FWUPD_VERBOSE") != NULL)
		g_debug("%s", dump);

	/* test for missing table */
	str = fu_smbios_get_string(smbios, 0xff, 0, &error);
	g_assert_error(error, FWUPD_ERROR, FWUPD_ERROR_INVALID_FILE);
	g_assert_null(str);
	g_clear_error(&error);

	/* check for invalid offset */
	str = fu_smbios_get_string(smbios, FU_SMBIOS_STRUCTURE_TYPE_BIOS, 0xff, &error);
	g_assert_error(error, FWUPD_ERROR, FWUPD_ERROR_INVALID_FILE);
	g_assert_null(str);
	g_clear_error(&error);

	/* get vendor */
	str = fu_smbios_get_string(smbios, FU_SMBIOS_STRUCTURE_TYPE_BIOS, 0x04, &error);
	g_assert_no_error(error);
	g_assert_cmpstr(str, ==, "LENOVO");
}

static void
fu_smbios3_func(void)
{
	const gchar *str;
	gboolean ret;
	g_autofree gchar *path = NULL;
	g_autoptr(FuSmbios) smbios = NULL;
	g_autoptr(GError) error = NULL;

	path = g_test_build_filename(G_TEST_DIST, "tests", "dmi", "tables64", NULL);
	smbios = fu_smbios_new();
	ret = fu_smbios_setup_from_path(smbios, path, &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	if (g_getenv("FWUPD_VERBOSE") != NULL) {
		g_autofree gchar *dump = fu_firmware_to_string(FU_FIRMWARE(smbios));
		g_debug("%s", dump);
	}

	/* get vendor */
	str = fu_smbios_get_string(smbios, FU_SMBIOS_STRUCTURE_TYPE_BIOS, 0x04, &error);
	g_assert_no_error(error);
	g_assert_cmpstr(str, ==, "Dell Inc.");
}

static void
fu_smbios_dt_func(void)
{
	const gchar *str;
	gboolean ret;
	g_autofree gchar *path = NULL;
	g_autoptr(FuSmbios) smbios = NULL;
	g_autoptr(GError) error = NULL;

	path = g_test_build_filename(G_TEST_DIST, "tests", "devicetree", "base", NULL);
	smbios = fu_smbios_new();
	ret = fu_smbios_setup_from_path(smbios, path, &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	if (g_getenv("FWUPD_VERBOSE") != NULL) {
		g_autofree gchar *dump = fu_firmware_to_string(FU_FIRMWARE(smbios));
		g_debug("%s", dump);
	}

	/* get vendor */
	str = fu_smbios_get_string(smbios, FU_SMBIOS_STRUCTURE_TYPE_SYSTEM, 0x04, &error);
	g_assert_no_error(error);
	g_assert_cmpstr(str, ==, "Hughski Limited");
}

static void
fu_smbios_dt_fallback_func(void)
{
	const gchar *str;
	gboolean ret;
	g_autofree gchar *path = NULL;
	g_autoptr(FuSmbios) smbios = fu_smbios_new();
	g_autoptr(GError) error = NULL;

	path = g_test_build_filename(G_TEST_DIST, "tests", "devicetree-fallback", "base", NULL);
	ret = fu_smbios_setup_from_path(smbios, path, &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	if (g_getenv("FWUPD_VERBOSE") != NULL) {
		g_autofree gchar *dump = fu_firmware_to_string(FU_FIRMWARE(smbios));
		g_debug("%s", dump);
	}

	/* get vendor */
	str = fu_smbios_get_string(smbios, FU_SMBIOS_STRUCTURE_TYPE_SYSTEM, 0x04, &error);
	g_assert_no_error(error);
	g_assert_cmpstr(str, ==, "solidrun");

	/* get model */
	str = fu_smbios_get_string(smbios, FU_SMBIOS_STRUCTURE_TYPE_SYSTEM, 0x05, &error);
	g_assert_no_error(error);
	g_assert_cmpstr(str, ==, "honeycomb");
}

static void
fu_smbios_class_func(void)
{
	g_autofree gchar *path = g_test_build_filename(G_TEST_DIST, "tests", "dmi", "class", NULL);
	g_autoptr(FuSmbios) smbios = fu_smbios_new();
	g_autoptr(GError) error = NULL;
	gboolean ret;
	const gchar *str;
	guint8 byte;

	ret = fu_smbios_setup_from_kernel(smbios, path, &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	if (g_getenv("FWUPD_VERBOSE") != NULL) {
		g_autofree gchar *dump = fu_firmware_to_string(FU_FIRMWARE(smbios));
		g_debug("%s", dump);
	}

	str = fu_smbios_get_string(smbios, FU_SMBIOS_STRUCTURE_TYPE_SYSTEM, 4, &error);
	g_assert_no_error(error);
	g_assert_cmpstr(str, ==, "FwupdTest");

	byte = fu_smbios_get_integer(smbios, FU_SMBIOS_STRUCTURE_TYPE_CHASSIS, 5, &error);
	g_assert_no_error(error);
	g_assert_cmpuint(byte, ==, 16);
}

static gboolean
_strnsplit_add_cb(GString *token, guint token_idx, gpointer user_data, GError **error)
{
	GPtrArray *array = (GPtrArray *)user_data;
	g_debug("TOKEN: [%s] (%u)", token->str, token_idx);
	g_ptr_array_add(array, g_strdup(token->str));
	return TRUE;
}

static gboolean
_strnsplit_nop_cb(GString *token, guint token_idx, gpointer user_data, GError **error)
{
	guint *cnt = (guint *)user_data;
	(*cnt)++;
	return TRUE;
}

static void
fu_common_memmem_func(void)
{
	const guint8 haystack[] = {'H', 'A', 'Y', 'S'};
	const guint8 needle[] = {'A', 'Y'};
	gboolean ret;
	gsize offset = 0;
	g_autoptr(GError) error = NULL;

	ret = fu_memmem_safe(haystack, sizeof(haystack), needle, sizeof(needle), &offset, &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	g_assert_cmpint(offset, ==, 0x1);

	ret = fu_memmem_safe(haystack + 2,
			     sizeof(haystack) - 2,
			     needle,
			     sizeof(needle),
			     &offset,
			     &error);
	g_assert_error(error, FWUPD_ERROR, FWUPD_ERROR_NOT_FOUND);
	g_assert_false(ret);
}

static void
fu_strsplit_func(void)
{
	const gchar *str = "123foo123bar123";
	const guint bigsz = 1024 * 1024;
	gboolean ret;
	guint cnt = 0;
	g_autoptr(GError) error = NULL;
	g_autoptr(GPtrArray) array = g_ptr_array_new_with_free_func(g_free);
	g_autoptr(GString) bigstr = g_string_sized_new(bigsz * 2);

	/* works for me */
	ret = fu_strsplit_full(str, -1, "123", _strnsplit_add_cb, array, &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	g_assert_cmpint(array->len, ==, 3);
	g_assert_cmpstr(g_ptr_array_index(array, 0), ==, "");
	g_assert_cmpstr(g_ptr_array_index(array, 1), ==, "foo");
	g_assert_cmpstr(g_ptr_array_index(array, 2), ==, "bar");

	/* lets try something insane */
	for (guint i = 0; i < bigsz; i++)
		g_string_append(bigstr, "X\n");
	ret = fu_strsplit_full(bigstr->str, -1, "\n", _strnsplit_nop_cb, &cnt, &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	g_assert_cmpint(cnt, ==, bigsz);
}

static void
fu_strsafe_func(void)
{
	struct {
		const gchar *in;
		const gchar *op;
	} strs[] = {{"dave123", "dave123"},
		    {"dave123XXX", "dave123"},
		    {"dave\x03XXX", "dave.XX"},
		    {"dave\x03\x04XXX", "dave..X"},
		    {"\x03\x03", NULL},
		    {NULL, NULL}};
	GPtrArray *instance_ids;
	gboolean ret;
	g_autoptr(FuContext) ctx = fu_context_new();
	g_autoptr(FuDevice) dev = fu_device_new(ctx);
	g_autoptr(GError) error = NULL;

	/* check bespoke legacy instance ID behavior */
	fu_device_add_instance_strsafe(dev, "KEY", "_ _LEN&VO&\\&");
	ret = fu_device_build_instance_id(dev, &error, "SUB", "KEY", NULL);
	g_assert_no_error(error);
	g_assert_true(ret);
	instance_ids = fu_device_get_instance_ids(dev);
	g_assert_cmpint(instance_ids->len, ==, 1);
	g_assert_cmpstr(g_ptr_array_index(instance_ids, 0), ==, "SUB\\KEY_LEN-VO");

	for (guint i = 0; strs[i].in != NULL; i++) {
		g_autofree gchar *tmp = fu_strsafe(strs[i].in, 7);
		g_assert_cmpstr(tmp, ==, strs[i].op);
	}
}

static void
fu_hwids_func(void)
{
	g_autofree gchar *testdatadir = NULL;
	g_autoptr(FuHwids) hwids = NULL;
	g_autoptr(FuSmbios) smbios = NULL;
	g_autoptr(GError) error = NULL;
	gboolean ret;

	struct {
		const gchar *key;
		const gchar *value;
	} guids[] = {{"Manufacturer", "6de5d951-d755-576b-bd09-c5cf66b27234"},
		     {"HardwareID-14", "6de5d951-d755-576b-bd09-c5cf66b27234"},
		     {"HardwareID-13", "f8e1de5f-b68c-5f52-9d1a-f1ba52f1f773"},
		     {"HardwareID-12", "e093d715-70f7-51f4-b6c8-b4a7e31def85"},
		     {"HardwareID-11", "db73af4c-4612-50f7-b8a7-787cf4871847"},
		     {"HardwareID-10", "f4275c1f-6130-5191-845c-3426247eb6a1"},
		     {"HardwareID-9", "0cf8618d-9eff-537c-9f35-46861406eb9c"},
		     {"HardwareID-8", "059eb22d-6dc7-59af-abd3-94bbe017f67c"},
		     {"HardwareID-7", "da1da9b6-62f5-5f22-8aaa-14db7eeda2a4"},
		     {"HardwareID-6", "178cd22d-ad9f-562d-ae0a-34009822cdbe"},
		     {"HardwareID-5", "8dc9b7c5-f5d5-5850-9ab3-bd6f0549d814"},
		     {"HardwareID-4", "660ccba8-1b78-5a33-80e6-9fb8354ee873"},
		     {"HardwareID-3", "3faec92a-3ae3-5744-be88-495e90a7d541"},
		     {"HardwareID-2", "f5ff077f-3eeb-5bae-be1c-e98ffe8ce5f8"},
		     {"HardwareID-1", "b7cceb67-774c-537e-bf8b-22c6107e9a74"},
		     {"HardwareID-0", "147efce9-f201-5fc8-ab0c-c859751c3440"},
		     {NULL, NULL}};

#ifdef _WIN32
	g_test_skip("Windows uses GetSystemFirmwareTable rather than parsing the fake test data");
	return;
#endif

	/* these tests will not write */
	testdatadir = g_test_build_filename(G_TEST_DIST, "tests", NULL);
	(void)g_setenv("FWUPD_SYSFSFWDIR", testdatadir, TRUE);

	smbios = fu_smbios_new();
	ret = fu_smbios_setup(smbios, &error);
	g_assert_no_error(error);
	g_assert_true(ret);

	hwids = fu_hwids_new();
	ret = fu_hwids_setup(hwids, smbios, &error);
	g_assert_no_error(error);
	g_assert_true(ret);

	g_assert_cmpstr(fu_hwids_get_value(hwids, FU_HWIDS_KEY_MANUFACTURER), ==, "LENOVO");
	g_assert_cmpstr(fu_hwids_get_value(hwids, FU_HWIDS_KEY_ENCLOSURE_KIND), ==, "a");
	g_assert_cmpstr(fu_hwids_get_value(hwids, FU_HWIDS_KEY_FAMILY), ==, "ThinkPad T440s");
	g_assert_cmpstr(fu_hwids_get_value(hwids, FU_HWIDS_KEY_PRODUCT_NAME), ==, "20ARS19C0C");
	g_assert_cmpstr(fu_hwids_get_value(hwids, FU_HWIDS_KEY_BIOS_VENDOR), ==, "LENOVO");
	g_assert_cmpstr(fu_hwids_get_value(hwids, FU_HWIDS_KEY_BIOS_VERSION),
			==,
			"GJET75WW (2.25 )");
	g_assert_cmpstr(fu_hwids_get_value(hwids, FU_HWIDS_KEY_BIOS_MAJOR_RELEASE), ==, "02");
	g_assert_cmpstr(fu_hwids_get_value(hwids, FU_HWIDS_KEY_BIOS_MINOR_RELEASE), ==, "19");
	g_assert_cmpstr(fu_hwids_get_value(hwids, FU_HWIDS_KEY_PRODUCT_SKU),
			==,
			"LENOVO_MT_20AR_BU_Think_FM_ThinkPad T440s");
	for (guint i = 0; guids[i].key != NULL; i++) {
		g_autofree gchar *guid = fu_hwids_get_guid(hwids, guids[i].key, &error);
		g_assert_no_error(error);
		g_assert_cmpstr(guid, ==, guids[i].value);
	}
	for (guint i = 0; guids[i].key != NULL; i++)
		g_assert_true(fu_hwids_has_guid(hwids, guids[i].value));
}

static void
_plugin_device_added_cb(FuPlugin *plugin, FuDevice *device, gpointer user_data)
{
	FuDevice **dev = (FuDevice **)user_data;
	*dev = g_object_ref(device);
	fu_test_loop_quit();
}

static void
fu_plugin_config_func(void)
{
	GStatBuf statbuf = {0};
	gboolean ret;
	gint rc;
	g_autofree gchar *conf_dir = NULL;
	g_autofree gchar *conf_file = NULL;
	g_autofree gchar *fn = NULL;
	g_autofree gchar *testdatadir = NULL;
	g_autofree gchar *value = NULL;
	g_autoptr(FuPlugin) plugin = fu_plugin_new(NULL);
	g_autoptr(GError) error = NULL;

	/* this is a build file */
	testdatadir = g_test_build_filename(G_TEST_BUILT, "tests", NULL);
	(void)g_setenv("FWUPD_SYSCONFDIR", testdatadir, TRUE);
	conf_dir = fu_path_from_kind(FU_PATH_KIND_SYSCONFDIR_PKG);

	/* remove existing file */
	fu_plugin_set_name(plugin, "test");
	conf_file = g_strdup_printf("%s.conf", fu_plugin_get_name(plugin));
	fn = g_build_filename(conf_dir, conf_file, NULL);
	ret = fu_path_mkdir_parent(fn, &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	g_remove(fn);
	ret = g_file_set_contents(fn, "", -1, &error);
	g_assert_no_error(error);
	g_assert_true(ret);

	/* set a value */
	ret = fu_plugin_set_config_value(plugin, "Key", "True", &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	g_assert_true(g_file_test(fn, G_FILE_TEST_EXISTS));

	/* check it is world readable */
	rc = g_stat(fn, &statbuf);
	g_assert_cmpint(rc, ==, 0);
	g_assert_cmpint(statbuf.st_mode & 0777, ==, 0644);

	/* read back the value */
	value = fu_plugin_get_config_value(plugin, "Key");
	g_assert_cmpstr(value, ==, "True");
	g_assert_true(fu_plugin_get_config_value_boolean(plugin, "Key"));

	/* check it is private, i.e. only readable by the user/group */
	ret = fu_plugin_set_secure_config_value(plugin, "Key", "False", &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	rc = g_stat(fn, &statbuf);
	g_assert_cmpint(rc, ==, 0);
	g_assert_cmpint(statbuf.st_mode & 0777, ==, 0640);
}

static void
fu_plugin_devices_func(void)
{
	g_autoptr(FuDevice) device = fu_device_new(NULL);
	g_autoptr(FuDevice) child = fu_device_new(NULL);
	g_autoptr(FuPlugin) plugin = fu_plugin_new(NULL);
	GPtrArray *devices;

	devices = fu_plugin_get_devices(plugin);
	g_assert_nonnull(devices);
	g_assert_cmpint(devices->len, ==, 0);

	fu_device_set_id(device, "testdev");
	fu_device_set_name(device, "testdev");
	fu_plugin_device_add(plugin, device);
	g_assert_cmpint(devices->len, ==, 1);
	fu_plugin_device_remove(plugin, device);
	g_assert_cmpint(devices->len, ==, 0);

	/* add a child after adding the parent to the plugin */
	fu_device_set_id(child, "child");
	fu_device_set_name(child, "child");
	fu_device_add_child(device, child);
	g_assert_cmpint(devices->len, ==, 1);

	/* remove said child */
	fu_device_remove_child(device, child);
	g_assert_cmpint(devices->len, ==, 0);
}

static void
fu_plugin_device_inhibit_children_func(void)
{
	g_autoptr(FuDevice) parent = fu_device_new(NULL);
	g_autoptr(FuDevice) child1 = fu_device_new(NULL);
	g_autoptr(FuDevice) child2 = fu_device_new(NULL);

	fu_device_set_id(parent, "testdev");
	fu_device_set_name(parent, "testdev");
	fu_device_add_flag(parent, FWUPD_DEVICE_FLAG_UPDATABLE);
	fu_device_set_id(child1, "child1");
	fu_device_set_name(child1, "child1");
	fu_device_add_flag(child1, FWUPD_DEVICE_FLAG_UPDATABLE);
	fu_device_add_child(parent, child1);

	/* inhibit the parent */
	fu_device_inhibit(parent, "test", "because");
	g_assert_false(fu_device_has_flag(parent, FWUPD_DEVICE_FLAG_UPDATABLE));
	g_assert_true(fu_device_has_flag(child1, FWUPD_DEVICE_FLAG_UPDATABLE));
	fu_device_uninhibit(parent, "test");

	/* make the inhibit propagate to children */
	fu_device_add_internal_flag(parent, FU_DEVICE_INTERNAL_FLAG_INHIBIT_CHILDREN);
	fu_device_inhibit(parent, "test", "because");
	g_assert_false(fu_device_has_flag(parent, FWUPD_DEVICE_FLAG_UPDATABLE));
	g_assert_false(fu_device_has_flag(child1, FWUPD_DEVICE_FLAG_UPDATABLE));

	/* add a child after the inhibit, which should also be inhibited too */
	fu_device_set_id(child2, "child2");
	fu_device_set_name(child2, "child2");
	fu_device_add_flag(child2, FWUPD_DEVICE_FLAG_UPDATABLE);
	fu_device_add_child(parent, child2);
	g_assert_false(fu_device_has_flag(parent, FWUPD_DEVICE_FLAG_UPDATABLE));
	g_assert_false(fu_device_has_flag(child1, FWUPD_DEVICE_FLAG_UPDATABLE));
	g_assert_false(fu_device_has_flag(child2, FWUPD_DEVICE_FLAG_UPDATABLE));
}

static void
fu_plugin_delay_func(void)
{
	FuDevice *device_tmp;
	g_autoptr(FuPlugin) plugin = NULL;
	g_autoptr(FuDevice) device = NULL;

	plugin = fu_plugin_new(NULL);
	g_signal_connect(FU_PLUGIN(plugin),
			 "device-added",
			 G_CALLBACK(_plugin_device_added_cb),
			 &device_tmp);
	g_signal_connect(FU_PLUGIN(plugin),
			 "device-removed",
			 G_CALLBACK(_plugin_device_added_cb),
			 &device_tmp);

	/* add device straight away */
	device = fu_device_new(NULL);
	fu_device_set_id(device, "testdev");
	fu_plugin_device_add(plugin, device);
	g_assert_nonnull(device_tmp);
	g_assert_cmpstr(fu_device_get_id(device_tmp),
			==,
			"b7eccd0059d6d7dc2ef76c35d6de0048cc8c029d");
	g_clear_object(&device_tmp);

	/* remove device */
	fu_plugin_device_remove(plugin, device);
	g_assert_nonnull(device_tmp);
	g_assert_cmpstr(fu_device_get_id(device_tmp),
			==,
			"b7eccd0059d6d7dc2ef76c35d6de0048cc8c029d");
	g_clear_object(&device_tmp);
}

static void
fu_plugin_quirks_func(void)
{
	const gchar *tmp;
	gboolean ret;
	g_autoptr(FuContext) ctx = fu_context_new();
	g_autoptr(GError) error = NULL;

	ret = fu_context_load_quirks(ctx, FU_QUIRKS_LOAD_FLAG_NO_CACHE, &error);
	g_assert_no_error(error);
	g_assert_true(ret);

	/* USB\\VID_0A5C&PID_6412 */
	tmp = fu_context_lookup_quirk_by_id(ctx, "7a1ba7b9-6bcd-54a4-8a36-d60cc5ee935c", "Flags");
	g_assert_cmpstr(tmp, ==, "ignore-runtime");

	/* ACME Inc.=True */
	tmp = fu_context_lookup_quirk_by_id(ctx, "ec77e295-7c63-5935-9957-be0472d9593a", "Name");
	g_assert_cmpstr(tmp, ==, "awesome");

	/* CORP* */
	tmp = fu_context_lookup_quirk_by_id(ctx, "3731cce4-484c-521f-a652-892c8e0a65c7", "Name");
	g_assert_cmpstr(tmp, ==, "town");

	/* baz */
	tmp = fu_context_lookup_quirk_by_id(ctx, "579a3b1c-d1db-5bdc-b6b9-e2c1b28d5b8a", "Unfound");
	g_assert_cmpstr(tmp, ==, NULL);

	/* unfound */
	tmp = fu_context_lookup_quirk_by_id(ctx, "8ff2ed23-b37e-5f61-b409-b7fe9563be36", "tests");
	g_assert_cmpstr(tmp, ==, NULL);

	/* unfound */
	tmp = fu_context_lookup_quirk_by_id(ctx, "8ff2ed23-b37e-5f61-b409-b7fe9563be36", "unfound");
	g_assert_cmpstr(tmp, ==, NULL);

	/* GUID */
	tmp = fu_context_lookup_quirk_by_id(ctx, "bb9ec3e2-77b3-53bc-a1f1-b05916715627", "Flags");
	g_assert_cmpstr(tmp, ==, "clever");
}

static void
fu_plugin_quirks_performance_func(void)
{
	gboolean ret;
	g_autoptr(FuQuirks) quirks = fu_quirks_new();
	g_autoptr(GTimer) timer = g_timer_new();
	g_autoptr(GError) error = NULL;
	const gchar *keys[] = {"Name", "Children", "Flags", NULL};

	ret = fu_quirks_load(quirks, FU_QUIRKS_LOAD_FLAG_NO_CACHE, &error);
	g_assert_no_error(error);
	g_assert_true(ret);

	/* lookup */
	g_timer_reset(timer);
	for (guint j = 0; j < 1000; j++) {
		const gchar *group = "bb9ec3e2-77b3-53bc-a1f1-b05916715627";
		for (guint i = 0; keys[i] != NULL; i++) {
			const gchar *tmp = fu_quirks_lookup_by_id(quirks, group, keys[i]);
			g_assert_cmpstr(tmp, !=, NULL);
		}
	}
	g_print("lookup=%.3fms ", g_timer_elapsed(timer, NULL) * 1000.f);
}

static void
fu_plugin_quirks_device_func(void)
{
	FuDevice *device_tmp;
	GPtrArray *children;
	gboolean ret;
	g_autoptr(FuDevice) device = fu_device_new(NULL);
	g_autoptr(FuContext) ctx = fu_context_new();
	g_autoptr(GError) error = NULL;

	ret = fu_context_load_quirks(ctx, FU_QUIRKS_LOAD_FLAG_NO_CACHE, &error);
	g_assert_no_error(error);
	g_assert_true(ret);

	/* use quirk file to set device attributes */
	fu_device_set_physical_id(device, "usb:00:05");
	fu_device_set_context(device, ctx);
	fu_device_add_flag(device, FWUPD_DEVICE_FLAG_UPDATABLE);
	fu_device_add_instance_id(device, "USB\\VID_0BDA&PID_1100");
	fu_device_convert_instance_ids(device);
	g_assert_cmpstr(fu_device_get_name(device), ==, "Hub");

	/* ensure children are created */
	children = fu_device_get_children(device);
	g_assert_cmpint(children->len, ==, 1);
	device_tmp = g_ptr_array_index(children, 0);
	g_assert_cmpstr(fu_device_get_name(device_tmp), ==, "HDMI");
	g_assert_true(fu_device_has_flag(device_tmp, FWUPD_DEVICE_FLAG_UPDATABLE));
}

static void
fu_common_kernel_lockdown_func(void)
{
	gboolean ret;
	g_autofree gchar *locked_dir = NULL;
	g_autofree gchar *none_dir = NULL;
	g_autofree gchar *old_kernel_dir = NULL;

#ifndef __linux__
	g_test_skip("only works on Linux");
	return;
#endif

	old_kernel_dir = g_test_build_filename(G_TEST_DIST, "tests", "lockdown", NULL);
	(void)g_setenv("FWUPD_SYSFSSECURITYDIR", old_kernel_dir, TRUE);
	ret = fu_kernel_locked_down();
	g_assert_false(ret);

	locked_dir = g_test_build_filename(G_TEST_DIST, "tests", "lockdown", "locked", NULL);
	(void)g_setenv("FWUPD_SYSFSSECURITYDIR", locked_dir, TRUE);
	ret = fu_kernel_locked_down();
	g_assert_true(ret);

	none_dir = g_test_build_filename(G_TEST_DIST, "tests", "lockdown", "none", NULL);
	(void)g_setenv("FWUPD_SYSFSSECURITYDIR", none_dir, TRUE);
	ret = fu_kernel_locked_down();
	g_assert_false(ret);
}

static gboolean
_open_cb(GObject *device, GError **error)
{
	g_assert_cmpstr(g_object_get_data(device, "state"), ==, "closed");
	g_object_set_data(device, "state", (gpointer) "opened");
	return TRUE;
}

static gboolean
_close_cb(GObject *device, GError **error)
{
	g_assert_cmpstr(g_object_get_data(device, "state"), ==, "opened");
	g_object_set_data(device, "state", (gpointer) "closed-on-unref");
	return TRUE;
}

static void
fu_device_locker_func(void)
{
	g_autoptr(FuDeviceLocker) locker = NULL;
	g_autoptr(GError) error = NULL;
	g_autoptr(GObject) device = g_object_new(G_TYPE_OBJECT, NULL);

	g_object_set_data(device, "state", (gpointer) "closed");
	locker = fu_device_locker_new_full(device, _open_cb, _close_cb, &error);
	g_assert_no_error(error);
	g_assert_nonnull(locker);
	g_clear_object(&locker);
	g_assert_cmpstr(g_object_get_data(device, "state"), ==, "closed-on-unref");
}

static gboolean
_fail_open_cb(FuDevice *device, GError **error)
{
	fu_device_set_metadata_boolean(device, "Test::Open", TRUE);
	g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_FAILED, "fail");
	return FALSE;
}

static gboolean
_fail_close_cb(FuDevice *device, GError **error)
{
	fu_device_set_metadata_boolean(device, "Test::Close", TRUE);
	g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_BUSY, "busy");
	return FALSE;
}

static void
fu_device_locker_fail_func(void)
{
	g_autoptr(FuDeviceLocker) locker = NULL;
	g_autoptr(GError) error = NULL;
	g_autoptr(FuDevice) device = fu_device_new(NULL);
	locker = fu_device_locker_new_full(device,
					   (FuDeviceLockerFunc)_fail_open_cb,
					   (FuDeviceLockerFunc)_fail_close_cb,
					   &error);
	g_assert_error(error, G_IO_ERROR, G_IO_ERROR_FAILED);
	g_assert_null(locker);
	g_assert_true(fu_device_get_metadata_boolean(device, "Test::Open"));
	g_assert_true(fu_device_get_metadata_boolean(device, "Test::Close"));
	g_assert_false(fu_device_has_internal_flag(device, FU_DEVICE_INTERNAL_FLAG_IS_OPEN));
}

static void
fu_common_endian_func(void)
{
	guint8 buf[3];

	fu_memwrite_uint16(buf, 0x1234, G_LITTLE_ENDIAN);
	g_assert_cmpint(buf[0], ==, 0x34);
	g_assert_cmpint(buf[1], ==, 0x12);
	g_assert_cmpint(fu_memread_uint16(buf, G_LITTLE_ENDIAN), ==, 0x1234);

	fu_memwrite_uint16(buf, 0x1234, G_BIG_ENDIAN);
	g_assert_cmpint(buf[0], ==, 0x12);
	g_assert_cmpint(buf[1], ==, 0x34);
	g_assert_cmpint(fu_memread_uint16(buf, G_BIG_ENDIAN), ==, 0x1234);

	fu_memwrite_uint24(buf, 0x123456, G_LITTLE_ENDIAN);
	g_assert_cmpint(buf[0], ==, 0x56);
	g_assert_cmpint(buf[1], ==, 0x34);
	g_assert_cmpint(buf[2], ==, 0x12);
	g_assert_cmpint(fu_memread_uint24(buf, G_LITTLE_ENDIAN), ==, 0x123456);

	fu_memwrite_uint24(buf, 0x123456, G_BIG_ENDIAN);
	g_assert_cmpint(buf[0], ==, 0x12);
	g_assert_cmpint(buf[1], ==, 0x34);
	g_assert_cmpint(buf[2], ==, 0x56);
	g_assert_cmpint(fu_memread_uint24(buf, G_BIG_ENDIAN), ==, 0x123456);
}

static void
fu_common_cabinet_func(void)
{
	g_autoptr(FuCabinet) cabinet = fu_cabinet_new();
	g_autoptr(GBytes) blob1 = NULL;
	g_autoptr(GBytes) blob2 = NULL;
	g_autoptr(GBytes) jcat_blob1 = g_bytes_new_static("hello", 6);
	g_autoptr(GBytes) jcat_blob2 = g_bytes_new_static("hellX", 6);
	g_autoptr(GError) error = NULL;

	/* add */
	fu_cabinet_add_file(cabinet, "firmware.jcat", jcat_blob1);

	/* replace */
	fu_cabinet_add_file(cabinet, "firmware.jcat", jcat_blob2);

	/* get data */
	blob1 = fu_cabinet_get_file(cabinet, "firmware.jcat", &error);
	g_assert_no_error(error);
	g_assert_nonnull(blob1);
	g_assert_cmpstr(g_bytes_get_data(blob1, NULL), ==, "hellX");

	/* get data that does not exist */
	blob2 = fu_cabinet_get_file(cabinet, "foo.jcat", &error);
	g_assert_error(error, FWUPD_ERROR, FWUPD_ERROR_INVALID_FILE);
	g_assert_null(blob2);
}

static void
fu_common_bytes_get_data_func(void)
{
	const gchar *fn = "/tmp/fwupdzero";
	const guint8 *buf;
	gboolean ret;
	g_autoptr(GBytes) bytes1 = NULL;
	g_autoptr(GBytes) bytes2 = NULL;
	g_autoptr(GError) error = NULL;
	g_autoptr(GMappedFile) mmap = NULL;

	/* create file with zero size */
	ret = g_file_set_contents(fn, NULL, 0, &error);
	g_assert_no_error(error);
	g_assert_true(ret);

	/* check we got zero sized data */
	bytes1 = fu_bytes_get_contents(fn, &error);
	g_assert_no_error(error);
	g_assert_nonnull(bytes1);
	g_assert_cmpint(g_bytes_get_size(bytes1), ==, 0);
	g_assert_nonnull(g_bytes_get_data(bytes1, NULL));

	/* do the same with an mmap mapping, which returns NULL on empty file */
	mmap = g_mapped_file_new(fn, FALSE, &error);
	g_assert_no_error(error);
	g_assert_nonnull(mmap);
	bytes2 = g_mapped_file_get_bytes(mmap);
	g_assert_nonnull(bytes2);
	g_assert_cmpint(g_bytes_get_size(bytes2), ==, 0);
	g_assert_null(g_bytes_get_data(bytes2, NULL));

	/* use the safe function */
	buf = fu_bytes_get_data_safe(bytes2, NULL, &error);
	g_assert_error(error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA);
	g_assert_null(buf);
}

static gboolean
fu_device_poll_cb(FuDevice *device, GError **error)
{
	guint64 cnt = fu_device_get_metadata_integer(device, "cnt");
	g_debug("poll cnt=%" G_GUINT64_FORMAT, cnt);
	fu_device_set_metadata_integer(device, "cnt", cnt + 1);
	return TRUE;
}

static void
fu_device_poll_func(void)
{
	g_autoptr(FuDevice) device = fu_device_new(NULL);
	FuDeviceClass *klass = FU_DEVICE_GET_CLASS(device);
	guint cnt;

	/* set up a 10ms poll */
	klass->poll = fu_device_poll_cb;
	fu_device_set_metadata_integer(device, "cnt", 0);
	fu_device_set_poll_interval(device, 10);
	fu_test_loop_run_with_timeout(100);
	fu_test_loop_quit();
	cnt = fu_device_get_metadata_integer(device, "cnt");
	g_assert_cmpint(cnt, >=, 8);

	/* disable the poll */
	fu_device_set_poll_interval(device, 0);
	fu_test_loop_run_with_timeout(100);
	fu_test_loop_quit();
	g_assert_cmpint(fu_device_get_metadata_integer(device, "cnt"), ==, cnt);
}

static void
fu_device_func(void)
{
	g_autoptr(FuDevice) device = fu_device_new(NULL);
	g_autoptr(GPtrArray) possible_plugins = NULL;

	/* only add one plugin name of the same type */
	fu_device_add_possible_plugin(device, "test");
	fu_device_add_possible_plugin(device, "test");
	possible_plugins = fu_device_get_possible_plugins(device);
	g_assert_cmpint(possible_plugins->len, ==, 1);

	g_assert_cmpint(fu_device_get_backend_tags(device)->len, ==, 0);
	fu_device_add_backend_tag(device, "foo");
	fu_device_add_backend_tag(device, "bar");
	g_assert_cmpint(fu_device_get_backend_tags(device)->len, ==, 2);
	g_assert_true(fu_device_has_backend_tag(device, "foo"));
	g_assert_false(fu_device_has_backend_tag(device, "bazbazbazbazbaz"));
}

static void
fu_device_instance_ids_func(void)
{
	gboolean ret;
	g_autoptr(FuContext) ctx = fu_context_new();
	g_autoptr(FuDevice) device = fu_device_new(ctx);
	g_autoptr(GError) error = NULL;

	/* do not save silo */
	ret = fu_context_load_quirks(ctx, FU_QUIRKS_LOAD_FLAG_NO_CACHE, &error);
	g_assert_no_error(error);
	g_assert_true(ret);

	/* sanity check */
	g_assert_false(fu_device_has_guid(device, "c0a26214-223b-572a-9477-cde897fe8619"));

	/* add a deferred instance ID that only gets converted on ->setup */
	fu_device_add_instance_id(device, "foobarbaz");
	g_assert_false(fu_device_has_guid(device, "c0a26214-223b-572a-9477-cde897fe8619"));

	ret = fu_device_setup(device, &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	g_assert_true(fu_device_has_guid(device, "c0a26214-223b-572a-9477-cde897fe8619"));

	/* this gets added immediately */
	fu_device_add_instance_id(device, "bazbarfoo");
	g_assert_true(fu_device_has_guid(device, "77e49bb0-2cd6-5faf-bcee-5b7fbe6e944d"));
}

static void
fu_device_composite_id_func(void)
{
	g_autoptr(FuDevice) dev1 = fu_device_new(NULL);
	g_autoptr(FuDevice) dev2 = fu_device_new(NULL);
	g_autoptr(FuDevice) dev3 = fu_device_new(NULL);
	g_autoptr(FuDevice) dev4 = fu_device_new(NULL);

	/* single device */
	fu_device_set_id(dev1, "dev1");
	g_assert_cmpstr(fu_device_get_composite_id(dev1),
			==,
			"3b42553c4e3241e8f3f8fbc19a69fa2f95708a9d");
	fu_device_set_id(dev2, "dev2");

	/* one child */
	fu_device_add_child(dev1, dev2);
	g_assert_cmpstr(fu_device_get_composite_id(dev1),
			==,
			"3b42553c4e3241e8f3f8fbc19a69fa2f95708a9d");
	g_assert_cmpstr(fu_device_get_composite_id(dev2),
			==,
			"3b42553c4e3241e8f3f8fbc19a69fa2f95708a9d");

	/* add a different "family" */
	fu_device_set_id(dev3, "dev3");
	fu_device_set_id(dev4, "dev4");
	fu_device_add_child(dev3, dev4);
	fu_device_add_child(dev2, dev3);
	g_assert_cmpstr(fu_device_get_composite_id(dev1),
			==,
			"3b42553c4e3241e8f3f8fbc19a69fa2f95708a9d");
	g_assert_cmpstr(fu_device_get_composite_id(dev2),
			==,
			"3b42553c4e3241e8f3f8fbc19a69fa2f95708a9d");
	g_assert_cmpstr(fu_device_get_composite_id(dev3),
			==,
			"3b42553c4e3241e8f3f8fbc19a69fa2f95708a9d");
	g_assert_cmpstr(fu_device_get_composite_id(dev4),
			==,
			"3b42553c4e3241e8f3f8fbc19a69fa2f95708a9d");

	/* change the parent ID */
	fu_device_set_id(dev1, "dev1-NEW");
	g_assert_cmpstr(fu_device_get_composite_id(dev1),
			==,
			"a4c8efc6a0a58c2dc14c05fd33186703f7352997");
	g_assert_cmpstr(fu_device_get_composite_id(dev2),
			==,
			"a4c8efc6a0a58c2dc14c05fd33186703f7352997");
}

static void
fu_device_inhibit_func(void)
{
	g_autoptr(FuDevice) device = fu_device_new(NULL);

	fu_device_add_flag(device, FWUPD_DEVICE_FLAG_UPDATABLE);
	fu_device_set_battery_threshold(device, 25);
	g_assert_true(fu_device_has_flag(device, FWUPD_DEVICE_FLAG_UPDATABLE));
	g_assert_false(fu_device_has_flag(device, FWUPD_DEVICE_FLAG_UPDATABLE_HIDDEN));

	/* does not exist -> fine */
	fu_device_uninhibit(device, "NOTGOINGTOEXIST");
	g_assert_false(fu_device_has_inhibit(device, "NOTGOINGTOEXIST"));

	/* first one */
	fu_device_inhibit(device, "needs-activation", "Device is pending activation");
	g_assert_true(fu_device_has_inhibit(device, "needs-activation"));
	g_assert_true(fu_device_has_flag(device, FWUPD_DEVICE_FLAG_UPDATABLE_HIDDEN));
	g_assert_false(fu_device_has_flag(device, FWUPD_DEVICE_FLAG_UPDATABLE));

	/* another */
	fu_device_set_battery_level(device, 5);
	g_assert_true(fu_device_has_flag(device, FWUPD_DEVICE_FLAG_UPDATABLE_HIDDEN));
	g_assert_false(fu_device_has_flag(device, FWUPD_DEVICE_FLAG_UPDATABLE));

	/* activated, power still too low */
	fu_device_uninhibit(device, "needs-activation");
	g_assert_false(fu_device_has_inhibit(device, "needs-activation"));
	g_assert_true(fu_device_has_flag(device, FWUPD_DEVICE_FLAG_UPDATABLE_HIDDEN));
	g_assert_false(fu_device_has_flag(device, FWUPD_DEVICE_FLAG_UPDATABLE));

	/* we got some more power -> fine */
	fu_device_set_battery_level(device, 95);
	g_assert_true(fu_device_has_flag(device, FWUPD_DEVICE_FLAG_UPDATABLE));
	g_assert_false(fu_device_has_flag(device, FWUPD_DEVICE_FLAG_UPDATABLE_HIDDEN));
}

static void
fu_device_inhibit_updateable_func(void)
{
	g_autoptr(FuDevice) device = fu_device_new(NULL);

	g_assert_false(fu_device_has_flag(device, FWUPD_DEVICE_FLAG_UPDATABLE));
	g_assert_false(fu_device_has_flag(device, FWUPD_DEVICE_FLAG_UPDATABLE_HIDDEN));
	g_assert_cmpstr(fu_device_get_update_error(device), ==, NULL);

	/* first one */
	fu_device_inhibit(device, "needs-activation", "Device is pending activation");
	g_assert_false(fu_device_has_flag(device, FWUPD_DEVICE_FLAG_UPDATABLE_HIDDEN));
	g_assert_false(fu_device_has_flag(device, FWUPD_DEVICE_FLAG_UPDATABLE));
	g_assert_cmpstr(fu_device_get_update_error(device), ==, "Device is pending activation");

	/* activated, but still not updatable */
	fu_device_uninhibit(device, "needs-activation");
	g_assert_false(fu_device_has_flag(device, FWUPD_DEVICE_FLAG_UPDATABLE));
	g_assert_false(fu_device_has_flag(device, FWUPD_DEVICE_FLAG_UPDATABLE_HIDDEN));
	g_assert_cmpstr(fu_device_get_update_error(device), ==, NULL);
}

#define TEST_FLAG_FOO (1 << 0)
#define TEST_FLAG_BAR (1 << 1)
#define TEST_FLAG_BAZ (1 << 2)

static void
fu_device_private_flags_func(void)
{
	g_autofree gchar *tmp = NULL;
	g_autoptr(FuDevice) device = fu_device_new(NULL);

	fu_device_register_private_flag(device, TEST_FLAG_FOO, "foo");
	fu_device_register_private_flag(device, TEST_FLAG_BAR, "bar");

	fu_device_set_custom_flags(device, "foo");
	g_assert_cmpint(fu_device_get_private_flags(device), ==, TEST_FLAG_FOO);
	fu_device_set_custom_flags(device, "bar");
	g_assert_cmpint(fu_device_get_private_flags(device), ==, TEST_FLAG_FOO | TEST_FLAG_BAR);
	fu_device_set_custom_flags(device, "~bar");
	g_assert_cmpint(fu_device_get_private_flags(device), ==, TEST_FLAG_FOO);
	fu_device_set_custom_flags(device, "baz");
	g_assert_cmpint(fu_device_get_private_flags(device), ==, TEST_FLAG_FOO);
	fu_device_add_private_flag(device, TEST_FLAG_BAZ);
	g_assert_cmpint(fu_device_get_private_flags(device), ==, TEST_FLAG_FOO | TEST_FLAG_BAZ);

	tmp = fu_device_to_string(device);
	g_assert_cmpstr(tmp,
			==,
			"FuDevice:\n"
			"  Flags:                none\n"
			"  AcquiesceDelay:       50\n"
			"  CustomFlags:          baz\n"
			"  PrivateFlags:         foo\n");
}

static void
fu_device_flags_func(void)
{
	g_autoptr(FuDevice) device = fu_device_new(NULL);

	/* bitfield */
	for (guint64 i = 1; i < FU_DEVICE_INTERNAL_FLAG_UNKNOWN; i *= 2) {
		const gchar *tmp = fu_device_internal_flag_to_string(i);
		if (tmp == NULL)
			break;
		g_assert_cmpint(fu_device_internal_flag_from_string(tmp), ==, i);
	}

	g_assert_cmpint(fu_device_get_flags(device), ==, FWUPD_DEVICE_FLAG_NONE);

	/* remove IS_BOOTLOADER if is a BOOTLOADER */
	fu_device_add_flag(device, FWUPD_DEVICE_FLAG_IS_BOOTLOADER);
	fu_device_add_flag(device, FWUPD_DEVICE_FLAG_NEEDS_BOOTLOADER);
	g_assert_cmpint(fu_device_get_flags(device), ==, FWUPD_DEVICE_FLAG_NEEDS_BOOTLOADER);
	fu_device_remove_flag(device, FWUPD_DEVICE_FLAG_NEEDS_BOOTLOADER);

	/* check implication */
	fu_device_add_flag(device, FWUPD_DEVICE_FLAG_CAN_VERIFY_IMAGE);
	g_assert_cmpint(fu_device_get_flags(device),
			==,
			FWUPD_DEVICE_FLAG_CAN_VERIFY_IMAGE | FWUPD_DEVICE_FLAG_CAN_VERIFY);
	fu_device_remove_flag(device,
			      FWUPD_DEVICE_FLAG_CAN_VERIFY | FWUPD_DEVICE_FLAG_CAN_VERIFY_IMAGE);

	/* negation */
	fu_device_set_custom_flags(device, "is-bootloader,updatable");
	g_assert_cmpint(fu_device_get_flags(device),
			==,
			FWUPD_DEVICE_FLAG_IS_BOOTLOADER | FWUPD_DEVICE_FLAG_UPDATABLE);
	fu_device_set_custom_flags(device, "~is-bootloader");
	g_assert_cmpint(fu_device_get_flags(device), ==, FWUPD_DEVICE_FLAG_UPDATABLE);
}

static void
fu_device_children_func(void)
{
	gboolean ret;
	g_autoptr(FuContext) ctx = fu_context_new();
	g_autoptr(FuDevice) child = fu_device_new(NULL);
	g_autoptr(FuDevice) parent = fu_device_new(ctx);
	g_autoptr(GError) error = NULL;

	/* do not save silo */
	ret = fu_context_load_quirks(ctx, FU_QUIRKS_LOAD_FLAG_NO_CACHE, &error);
	g_assert_no_error(error);
	g_assert_true(ret);

	fu_device_set_physical_id(child, "dummy");
	fu_device_set_physical_id(parent, "dummy");

	/* set up family */
	fu_device_add_child(parent, child);

	/* set an instance ID that will be converted to a GUID when the parent
	 * calls ->setup */
	fu_device_add_instance_id(child, "foo");
	g_assert_false(fu_device_has_guid(child, "b84ed8ed-a7b1-502f-83f6-90132e68adef"));

	/* setup parent, which also calls setup on child too (and thus also
	 * converts the instance ID to a GUID) */
	ret = fu_device_setup(parent, &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	g_assert_true(fu_device_has_guid(child, "b84ed8ed-a7b1-502f-83f6-90132e68adef"));
}

static void
fu_device_parent_func(void)
{
	g_autoptr(FuDevice) child = fu_device_new(NULL);
	g_autoptr(FuDevice) child_root = NULL;
	g_autoptr(FuDevice) grandparent = fu_device_new(NULL);
	g_autoptr(FuDevice) grandparent_root = NULL;
	g_autoptr(FuDevice) parent = fu_device_new(NULL);
	g_autoptr(FuDevice) parent_root = NULL;

	fu_device_set_physical_id(child, "dummy");
	fu_device_set_physical_id(grandparent, "dummy");
	fu_device_set_physical_id(parent, "dummy");

	/* set up three layer family */
	fu_device_add_child(grandparent, parent);
	fu_device_add_child(parent, child);

	/* check parents */
	g_assert_true(fu_device_get_parent(child) == parent);
	g_assert_true(fu_device_get_parent(parent) == grandparent);
	g_assert_true(fu_device_get_parent(grandparent) == NULL);

	/* check root */
	child_root = fu_device_get_root(child);
	g_assert_true(child_root == grandparent);
	parent_root = fu_device_get_root(parent);
	g_assert_true(parent_root == grandparent);
	grandparent_root = fu_device_get_root(child);
	g_assert_true(grandparent_root == grandparent);
}

static void
fu_device_incorporate_func(void)
{
	gboolean ret;
	g_autoptr(FuContext) ctx = fu_context_new();
	g_autoptr(FuDevice) device = fu_device_new(ctx);
	g_autoptr(FuDevice) donor = fu_device_new(ctx);
	g_autoptr(GError) error = NULL;

	/* set up donor device */
	fu_device_set_alternate_id(donor, "alt-id");
	fu_device_set_equivalent_id(donor, "equiv-id");
	fu_device_set_metadata(donor, "test", "me");
	fu_device_set_metadata(donor, "test2", "me");
	fu_device_add_instance_str(donor, "VID", "1234");

	/* base properties */
	fu_device_add_flag(donor, FWUPD_DEVICE_FLAG_REQUIRE_AC);
	fu_device_set_created(donor, 123);
	fu_device_set_modified(donor, 456);
	fu_device_add_icon(donor, "computer");

	/* existing properties */
	fu_device_set_equivalent_id(device, "DO_NOT_OVERWRITE");
	fu_device_set_metadata(device, "test2", "DO_NOT_OVERWRITE");
	fu_device_set_modified(device, 789);

	/* incorporate properties from donor to device */
	fu_device_incorporate(device, donor);
	g_assert_cmpstr(fu_device_get_alternate_id(device), ==, "alt-id");
	g_assert_cmpstr(fu_device_get_equivalent_id(device), ==, "DO_NOT_OVERWRITE");
	g_assert_cmpstr(fu_device_get_metadata(device, "test"), ==, "me");
	g_assert_cmpstr(fu_device_get_metadata(device, "test2"), ==, "DO_NOT_OVERWRITE");
	g_assert_true(fu_device_has_flag(device, FWUPD_DEVICE_FLAG_REQUIRE_AC));
	g_assert_cmpint(fu_device_get_created(device), ==, 123);
	g_assert_cmpint(fu_device_get_modified(device), ==, 789);
	g_assert_cmpint(fu_device_get_icons(device)->len, ==, 1);
	ret = fu_device_build_instance_id(device, &error, "SUBSYS", "VID", NULL);
	g_assert_no_error(error);
	g_assert_true(ret);
	g_assert_true(fu_device_has_instance_id(device, "SUBSYS\\VID_1234"));
}

static void
fu_backend_func(void)
{
	FuDevice *dev;
	gboolean ret;
	g_autoptr(FuBackend) backend = g_object_new(FU_TYPE_BACKEND, NULL);
	g_autoptr(FuDevice) dev1 = fu_device_new(NULL);
	g_autoptr(FuDevice) dev2 = fu_device_new(NULL);
	g_autoptr(FuProgress) progress = fu_progress_new(G_STRLOC);
	g_autoptr(GError) error = NULL;
	g_autoptr(GPtrArray) devices = NULL;

	/* defaults */
	g_assert_null(fu_backend_get_name(backend));
	g_assert_true(fu_backend_get_enabled(backend));

	/* load */
	ret = fu_backend_setup(backend, progress, &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	ret = fu_backend_coldplug(backend, progress, &error);
	g_assert_no_error(error);
	g_assert_true(ret);

	/* add two devices, then remove one of them */
	fu_device_set_physical_id(dev1, "dev1");
	fu_backend_device_added(backend, dev1);
	fu_device_set_physical_id(dev2, "dev2");
	fu_backend_device_added(backend, dev2);
	fu_backend_device_changed(backend, dev2);
	fu_backend_device_removed(backend, dev2);

	dev = fu_backend_lookup_by_id(backend, "dev1");
	g_assert_nonnull(dev);
	g_assert_true(dev == dev1);

	/* should have been removed */
	dev = fu_backend_lookup_by_id(backend, "dev2");
	g_assert_null(dev);

	/* get linear array */
	devices = fu_backend_get_devices(backend);
	g_assert_nonnull(devices);
	g_assert_cmpint(devices->len, ==, 1);
	dev = g_ptr_array_index(devices, 0);
	g_assert_nonnull(dev);
	g_assert_true(dev == dev1);
}

static void
fu_chunk_func(void)
{
	g_autofree gchar *chunked1_str = NULL;
	g_autofree gchar *chunked2_str = NULL;
	g_autofree gchar *chunked3_str = NULL;
	g_autofree gchar *chunked4_str = NULL;
	g_autoptr(GPtrArray) chunked1 = NULL;
	g_autoptr(GPtrArray) chunked2 = NULL;
	g_autoptr(GPtrArray) chunked3 = NULL;
	g_autoptr(GPtrArray) chunked4 = NULL;

	chunked3 = fu_chunk_array_new((const guint8 *)"123456", 6, 0x0, 3, 3);
	chunked3_str = fu_chunk_array_to_string(chunked3);
	g_assert_cmpstr(chunked3_str,
			==,
			"<chunks>\n"
			"  <chunk>\n"
			"    <data size=\"0x3\">123</data>\n"
			"  </chunk>\n"
			"  <chunk>\n"
			"    <idx>0x1</idx>\n"
			"    <page>0x1</page>\n"
			"    <data size=\"0x3\">456</data>\n"
			"  </chunk>\n"
			"</chunks>\n");

	chunked4 = fu_chunk_array_new((const guint8 *)"123456", 6, 0x4, 4, 4);
	chunked4_str = fu_chunk_array_to_string(chunked4);
	g_assert_cmpstr(chunked4_str,
			==,
			"<chunks>\n"
			"  <chunk>\n"
			"    <page>0x1</page>\n"
			"    <data size=\"0x4\">1234</data>\n"
			"  </chunk>\n"
			"  <chunk>\n"
			"    <idx>0x1</idx>\n"
			"    <page>0x2</page>\n"
			"    <data size=\"0x2\">56</data>\n"
			"  </chunk>\n"
			"</chunks>\n");

	chunked1 = fu_chunk_array_new((const guint8 *)"0123456789abcdef", 16, 0x0, 10, 4);
	chunked1_str = fu_chunk_array_to_string(chunked1);
	g_assert_cmpstr(chunked1_str,
			==,
			"<chunks>\n"
			"  <chunk>\n"
			"    <data size=\"0x4\">0123</data>\n"
			"  </chunk>\n"
			"  <chunk>\n"
			"    <idx>0x1</idx>\n"
			"    <addr>0x4</addr>\n"
			"    <data size=\"0x4\">4567</data>\n"
			"  </chunk>\n"
			"  <chunk>\n"
			"    <idx>0x2</idx>\n"
			"    <addr>0x8</addr>\n"
			"    <data size=\"0x2\">89</data>\n"
			"  </chunk>\n"
			"  <chunk>\n"
			"    <idx>0x3</idx>\n"
			"    <page>0x1</page>\n"
			"    <data size=\"0x4\">abcd</data>\n"
			"  </chunk>\n"
			"  <chunk>\n"
			"    <idx>0x4</idx>\n"
			"    <page>0x1</page>\n"
			"    <addr>0x4</addr>\n"
			"    <data size=\"0x2\">ef</data>\n"
			"  </chunk>\n"
			"</chunks>\n");
	chunked2 = fu_chunk_array_new((const guint8 *)"XXXXXXYYYYYYZZZZZZ", 18, 0x0, 6, 4);
	chunked2_str = fu_chunk_array_to_string(chunked2);
	g_print("\n%s", chunked2_str);
	g_assert_cmpstr(chunked2_str,
			==,
			"<chunks>\n"
			"  <chunk>\n"
			"    <data size=\"0x4\">XXXX</data>\n"
			"  </chunk>\n"
			"  <chunk>\n"
			"    <idx>0x1</idx>\n"
			"    <addr>0x4</addr>\n"
			"    <data size=\"0x2\">XX</data>\n"
			"  </chunk>\n"
			"  <chunk>\n"
			"    <idx>0x2</idx>\n"
			"    <page>0x1</page>\n"
			"    <data size=\"0x4\">YYYY</data>\n"
			"  </chunk>\n"
			"  <chunk>\n"
			"    <idx>0x3</idx>\n"
			"    <page>0x1</page>\n"
			"    <addr>0x4</addr>\n"
			"    <data size=\"0x2\">YY</data>\n"
			"  </chunk>\n"
			"  <chunk>\n"
			"    <idx>0x4</idx>\n"
			"    <page>0x2</page>\n"
			"    <data size=\"0x4\">ZZZZ</data>\n"
			"  </chunk>\n"
			"  <chunk>\n"
			"    <idx>0x5</idx>\n"
			"    <page>0x2</page>\n"
			"    <addr>0x4</addr>\n"
			"    <data size=\"0x2\">ZZ</data>\n"
			"  </chunk>\n"
			"</chunks>\n");
}

static void
fu_strstrip_func(void)
{
	struct {
		const gchar *old;
		const gchar *new;
	} map[] = {{"same", "same"},
		   {" leading", "leading"},
		   {"tailing ", "tailing"},
		   {"  b  ", "b"},
		   {"  ", ""},
		   {NULL, NULL}};
	for (guint i = 0; map[i].old != NULL; i++) {
		g_autofree gchar *tmp = fu_strstrip(map[i].old);
		g_assert_cmpstr(tmp, ==, map[i].new);
	}
}

static void
fu_version_semver_func(void)
{
	struct {
		const gchar *old;
		const gchar *new;
		FwupdVersionFormat fmt;
	} map[] = {{"1.2.3", "1.2.3", FWUPD_VERSION_FORMAT_TRIPLET},
		   {"1.2.3.4", "1.2.3", FWUPD_VERSION_FORMAT_TRIPLET},
		   {"1.2", "0.1.2", FWUPD_VERSION_FORMAT_TRIPLET},
		   {"1", "0.0.1", FWUPD_VERSION_FORMAT_TRIPLET},
		   {"CBET1.2.3", "1.2.3", FWUPD_VERSION_FORMAT_TRIPLET},
		   {"4.11-1190-g12d8072e6b-dirty", "4.11.1190", FWUPD_VERSION_FORMAT_TRIPLET},
		   {"4.11-1190-g12d8072e6b-dirty", "4.11", FWUPD_VERSION_FORMAT_PAIR},
		   {NULL, NULL}};
	for (guint i = 0; map[i].old != NULL; i++) {
		g_autofree gchar *tmp = fu_version_ensure_semver(map[i].old, map[i].fmt);
		g_assert_cmpstr(tmp, ==, map[i].new);
	}
}

static void
fu_strtoull_func(void)
{
	gboolean ret;
	guint64 val = 0;
	g_autoptr(GError) error = NULL;

	ret = fu_strtoull("123", &val, 123, 200, &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	g_assert_cmpint(val, ==, 123);

	ret = fu_strtoull("123\n", &val, 0, 200, &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	g_assert_cmpint(val, ==, 123);

	ret = fu_strtoull("0x123", &val, 0, 0x123, &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	g_assert_cmpint(val, ==, 0x123);

	ret = fu_strtoull(NULL, &val, 0, G_MAXUINT32, NULL);
	g_assert_false(ret);
	ret = fu_strtoull("", &val, 120, 123, NULL);
	g_assert_false(ret);
	ret = fu_strtoull("124", &val, 120, 123, NULL);
	g_assert_false(ret);
	ret = fu_strtoull("119", &val, 120, 123, NULL);
	g_assert_false(ret);
}

static void
fu_common_version_func(void)
{
	guint i;
	struct {
		guint32 val;
		const gchar *ver;
		FwupdVersionFormat flags;
	} version_from_uint32[] = {
	    {0x0, "0.0.0.0", FWUPD_VERSION_FORMAT_QUAD},
	    {0xff, "0.0.0.255", FWUPD_VERSION_FORMAT_QUAD},
	    {0xff01, "0.0.255.1", FWUPD_VERSION_FORMAT_QUAD},
	    {0xff0001, "0.255.0.1", FWUPD_VERSION_FORMAT_QUAD},
	    {0xff000100, "255.0.1.0", FWUPD_VERSION_FORMAT_QUAD},
	    {0x0, "0.0.0", FWUPD_VERSION_FORMAT_TRIPLET},
	    {0xff, "0.0.255", FWUPD_VERSION_FORMAT_TRIPLET},
	    {0xff01, "0.0.65281", FWUPD_VERSION_FORMAT_TRIPLET},
	    {0xff0001, "0.255.1", FWUPD_VERSION_FORMAT_TRIPLET},
	    {0xff000100, "255.0.256", FWUPD_VERSION_FORMAT_TRIPLET},
	    {0x0, "0", FWUPD_VERSION_FORMAT_NUMBER},
	    {0xff000100, "4278190336", FWUPD_VERSION_FORMAT_NUMBER},
	    {0x0, "11.0.0.0", FWUPD_VERSION_FORMAT_INTEL_ME},
	    {0xffffffff, "18.31.255.65535", FWUPD_VERSION_FORMAT_INTEL_ME},
	    {0x0b32057a, "11.11.50.1402", FWUPD_VERSION_FORMAT_INTEL_ME},
	    {0xb8320d84, "11.8.50.3460", FWUPD_VERSION_FORMAT_INTEL_ME2},
	    {0x226a4b00, "137.2706.768", FWUPD_VERSION_FORMAT_SURFACE_LEGACY},
	    {0x6001988, "6.25.136", FWUPD_VERSION_FORMAT_SURFACE},
	    {0x00ff0001, "255.0.1", FWUPD_VERSION_FORMAT_DELL_BIOS},
	    {0xc8, "0x000000c8", FWUPD_VERSION_FORMAT_HEX},
	    {0, NULL}};
	struct {
		guint64 val;
		const gchar *ver;
		FwupdVersionFormat flags;
	} version_from_uint64[] = {
	    {0x0, "0.0.0.0", FWUPD_VERSION_FORMAT_QUAD},
	    {0xff, "0.0.0.255", FWUPD_VERSION_FORMAT_QUAD},
	    {0xffffffffffffffff, "65535.65535.65535.65535", FWUPD_VERSION_FORMAT_QUAD},
	    {0xff, "0.255", FWUPD_VERSION_FORMAT_PAIR},
	    {0xffffffffffffffff, "4294967295.4294967295", FWUPD_VERSION_FORMAT_PAIR},
	    {0x0, "0", FWUPD_VERSION_FORMAT_NUMBER},
	    {0x11000000c8, "0x00000011000000c8", FWUPD_VERSION_FORMAT_HEX},
	    {0, NULL}};
	struct {
		guint16 val;
		const gchar *ver;
		FwupdVersionFormat flags;
	} version_from_uint16[] = {{0x0, "0.0", FWUPD_VERSION_FORMAT_PAIR},
				   {0xff, "0.255", FWUPD_VERSION_FORMAT_PAIR},
				   {0xff01, "255.1", FWUPD_VERSION_FORMAT_PAIR},
				   {0x0, "0.0", FWUPD_VERSION_FORMAT_BCD},
				   {0x0110, "1.10", FWUPD_VERSION_FORMAT_BCD},
				   {0x9999, "99.99", FWUPD_VERSION_FORMAT_BCD},
				   {0x0, "0", FWUPD_VERSION_FORMAT_NUMBER},
				   {0x1234, "4660", FWUPD_VERSION_FORMAT_NUMBER},
				   {0, NULL}};
	struct {
		const gchar *old;
		const gchar *new;
	} version_parse[] = {{"0", "0"},
			     {"0x1a", "0.0.26"},
			     {"257", "0.0.257"},
			     {"1.2.3", "1.2.3"},
			     {"0xff0001", "0.255.1"},
			     {"16711681", "0.255.1"},
			     {"20150915", "20150915"},
			     {"dave", "dave"},
			     {"0x1x", "0x1x"},
			     {NULL, NULL}};

	/* check version conversion */
	for (i = 0; version_from_uint64[i].ver != NULL; i++) {
		g_autofree gchar *ver = NULL;
		ver = fu_version_from_uint64(version_from_uint64[i].val,
					     version_from_uint64[i].flags);
		g_assert_cmpstr(ver, ==, version_from_uint64[i].ver);
	}
	for (i = 0; version_from_uint32[i].ver != NULL; i++) {
		g_autofree gchar *ver = NULL;
		ver = fu_version_from_uint32(version_from_uint32[i].val,
					     version_from_uint32[i].flags);
		g_assert_cmpstr(ver, ==, version_from_uint32[i].ver);
	}
	for (i = 0; version_from_uint16[i].ver != NULL; i++) {
		g_autofree gchar *ver = NULL;
		ver = fu_version_from_uint16(version_from_uint16[i].val,
					     version_from_uint16[i].flags);
		g_assert_cmpstr(ver, ==, version_from_uint16[i].ver);
	}

	/* check version parsing */
	for (i = 0; version_parse[i].old != NULL; i++) {
		g_autofree gchar *ver = NULL;
		ver = fu_version_parse_from_format(version_parse[i].old,
						   FWUPD_VERSION_FORMAT_TRIPLET);
		g_assert_cmpstr(ver, ==, version_parse[i].new);
	}
}

static void
fu_common_vercmp_func(void)
{
	/* same */
	g_assert_cmpint(fu_version_compare("1.2.3", "1.2.3", FWUPD_VERSION_FORMAT_UNKNOWN), ==, 0);
	g_assert_cmpint(
	    fu_version_compare("001.002.003", "001.002.003", FWUPD_VERSION_FORMAT_UNKNOWN),
	    ==,
	    0);
	g_assert_cmpint(fu_version_compare("0x00000002", "0x2", FWUPD_VERSION_FORMAT_HEX), ==, 0);

	/* upgrade and downgrade */
	g_assert_cmpint(fu_version_compare("1.2.3", "1.2.4", FWUPD_VERSION_FORMAT_UNKNOWN), <, 0);
	g_assert_cmpint(
	    fu_version_compare("001.002.000", "001.002.009", FWUPD_VERSION_FORMAT_UNKNOWN),
	    <,
	    0);
	g_assert_cmpint(fu_version_compare("1.2.3", "1.2.2", FWUPD_VERSION_FORMAT_UNKNOWN), >, 0);
	g_assert_cmpint(
	    fu_version_compare("001.002.009", "001.002.000", FWUPD_VERSION_FORMAT_UNKNOWN),
	    >,
	    0);

	/* unequal depth */
	g_assert_cmpint(fu_version_compare("1.2.3", "1.2.3.1", FWUPD_VERSION_FORMAT_UNKNOWN), <, 0);
	g_assert_cmpint(fu_version_compare("1.2.3.1", "1.2.4", FWUPD_VERSION_FORMAT_UNKNOWN), <, 0);

	/* mixed-alpha-numeric */
	g_assert_cmpint(fu_version_compare("1.2.3a", "1.2.3a", FWUPD_VERSION_FORMAT_UNKNOWN),
			==,
			0);
	g_assert_cmpint(fu_version_compare("1.2.3a", "1.2.3b", FWUPD_VERSION_FORMAT_UNKNOWN), <, 0);
	g_assert_cmpint(fu_version_compare("1.2.3b", "1.2.3a", FWUPD_VERSION_FORMAT_UNKNOWN), >, 0);

	/* alpha version append */
	g_assert_cmpint(fu_version_compare("1.2.3", "1.2.3a", FWUPD_VERSION_FORMAT_UNKNOWN), <, 0);
	g_assert_cmpint(fu_version_compare("1.2.3a", "1.2.3", FWUPD_VERSION_FORMAT_UNKNOWN), >, 0);

	/* alpha only */
	g_assert_cmpint(fu_version_compare("alpha", "alpha", FWUPD_VERSION_FORMAT_UNKNOWN), ==, 0);
	g_assert_cmpint(fu_version_compare("alpha", "beta", FWUPD_VERSION_FORMAT_UNKNOWN), <, 0);
	g_assert_cmpint(fu_version_compare("beta", "alpha", FWUPD_VERSION_FORMAT_UNKNOWN), >, 0);

	/* alpha-compare */
	g_assert_cmpint(fu_version_compare("1.2a.3", "1.2a.3", FWUPD_VERSION_FORMAT_UNKNOWN),
			==,
			0);
	g_assert_cmpint(fu_version_compare("1.2a.3", "1.2b.3", FWUPD_VERSION_FORMAT_UNKNOWN), <, 0);
	g_assert_cmpint(fu_version_compare("1.2b.3", "1.2a.3", FWUPD_VERSION_FORMAT_UNKNOWN), >, 0);

	/* tilde is all-powerful */
	g_assert_cmpint(fu_version_compare("1.2.3~rc1", "1.2.3~rc1", FWUPD_VERSION_FORMAT_UNKNOWN),
			==,
			0);
	g_assert_cmpint(fu_version_compare("1.2.3~rc1", "1.2.3", FWUPD_VERSION_FORMAT_UNKNOWN),
			<,
			0);
	g_assert_cmpint(fu_version_compare("1.2.3", "1.2.3~rc1", FWUPD_VERSION_FORMAT_UNKNOWN),
			>,
			0);
	g_assert_cmpint(fu_version_compare("1.2.3~rc2", "1.2.3~rc1", FWUPD_VERSION_FORMAT_UNKNOWN),
			>,
			0);

	/* invalid */
	g_assert_cmpint(fu_version_compare("1", NULL, FWUPD_VERSION_FORMAT_UNKNOWN), ==, G_MAXINT);
	g_assert_cmpint(fu_version_compare(NULL, "1", FWUPD_VERSION_FORMAT_UNKNOWN), ==, G_MAXINT);
	g_assert_cmpint(fu_version_compare(NULL, NULL, FWUPD_VERSION_FORMAT_UNKNOWN), ==, G_MAXINT);
}

static void
fu_firmware_raw_aligned_func(void)
{
	gboolean ret;
	g_autoptr(FuFirmware) firmware1 = fu_firmware_new();
	g_autoptr(FuFirmware) firmware2 = fu_firmware_new();
	g_autoptr(GError) error = NULL;
	g_autoptr(GBytes) blob = g_bytes_new_static("hello", 5);

	/* no alignment */
	ret = fu_firmware_parse(firmware1, blob, FWUPD_INSTALL_FLAG_NO_SEARCH, &error);
	g_assert_no_error(error);
	g_assert_true(ret);

	/* invalid alignment */
	fu_firmware_set_alignment(firmware2, FU_FIRMWARE_ALIGNMENT_4K);
	ret = fu_firmware_parse(firmware2, blob, FWUPD_INSTALL_FLAG_NO_SEARCH, &error);
	g_assert_error(error, FWUPD_ERROR, FWUPD_ERROR_INVALID_FILE);
	g_assert_false(ret);
}

static void
fu_firmware_ihex_func(void)
{
	const guint8 *data;
	gboolean ret;
	gsize len;
	g_autofree gchar *filename_hex = NULL;
	g_autofree gchar *filename_ref = NULL;
	g_autofree gchar *str = NULL;
	g_autoptr(FuFirmware) firmware = fu_ihex_firmware_new();
	g_autoptr(GBytes) data_file = NULL;
	g_autoptr(GBytes) data_fw = NULL;
	g_autoptr(GBytes) data_hex = NULL;
	g_autoptr(GBytes) data_ref = NULL;
	g_autoptr(GError) error = NULL;

	/* load a Intel hex32 file */
	filename_hex = g_test_build_filename(G_TEST_DIST, "tests", "firmware.hex", NULL);
	data_file = fu_bytes_get_contents(filename_hex, &error);
	g_assert_no_error(error);
	g_assert_nonnull(data_file);
	ret = fu_firmware_parse(firmware, data_file, FWUPD_INSTALL_FLAG_NO_SEARCH, &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	data_fw = fu_firmware_get_bytes(firmware, &error);
	g_assert_no_error(error);
	g_assert_nonnull(data_fw);
	g_assert_cmpint(g_bytes_get_size(data_fw), ==, 136);

	/* did we match the reference file? */
	filename_ref = g_test_build_filename(G_TEST_DIST, "tests", "firmware.bin", NULL);
	data_ref = fu_bytes_get_contents(filename_ref, &error);
	g_assert_no_error(error);
	g_assert_nonnull(data_ref);
	ret = fu_bytes_compare(data_fw, data_ref, &error);
	g_assert_no_error(error);
	g_assert_true(ret);

	/* export a ihex file (which will be slightly different due to
	 * non-continous regions being expanded */
	data_hex = fu_firmware_write(firmware, &error);
	g_assert_no_error(error);
	g_assert_nonnull(data_hex);
	data = g_bytes_get_data(data_hex, &len);
	str = g_strndup((const gchar *)data, len);
	g_assert_cmpstr(str,
			==,
			":104000003DEF20F000000000FACF01F0FBCF02F0FE\n"
			":10401000E9CF03F0EACF04F0E1CF05F0E2CF06F0FC\n"
			":10402000D9CF07F0DACF08F0F3CF09F0F4CF0AF0D8\n"
			":10403000F6CF0BF0F7CF0CF0F8CF0DF0F5CF0EF078\n"
			":104040000EC0F5FF0DC0F8FF0CC0F7FF0BC0F6FF68\n"
			":104050000AC0F4FF09C0F3FF08C0DAFF07C0D9FFA8\n"
			":1040600006C0E2FF05C0E1FF04C0EAFF03C0E9FFAC\n"
			":1040700002C0FBFF01C0FAFF11003FEF20F000017A\n"
			":0840800042EF20F03DEF20F0BB\n"
			":00000001FF\n");
}

static void
fu_firmware_ihex_signed_func(void)
{
	const guint8 *data;
	gboolean ret;
	gsize len;
	g_autofree gchar *filename_shex = NULL;
	g_autoptr(FuFirmware) firmware = fu_ihex_firmware_new();
	g_autoptr(GBytes) data_file = NULL;
	g_autoptr(GBytes) data_fw = NULL;
	g_autoptr(GBytes) data_sig = NULL;
	g_autoptr(GError) error = NULL;

	/* load a signed Intel hex32 file */
	filename_shex = g_test_build_filename(G_TEST_DIST, "tests", "firmware.shex", NULL);
	data_file = fu_bytes_get_contents(filename_shex, &error);
	g_assert_no_error(error);
	g_assert_nonnull(data_file);
	ret = fu_firmware_parse(firmware, data_file, FWUPD_INSTALL_FLAG_NO_SEARCH, &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	data_fw = fu_firmware_get_bytes(firmware, &error);
	g_assert_no_error(error);
	g_assert_nonnull(data_fw);
	g_assert_cmpint(g_bytes_get_size(data_fw), ==, 136);

	/* get the signed image */
	data_sig = fu_firmware_get_image_by_id_bytes(firmware, FU_FIRMWARE_ID_SIGNATURE, &error);
	g_assert_no_error(error);
	g_assert_nonnull(data_sig);
	data = g_bytes_get_data(data_sig, &len);
	g_assert_cmpint(len, ==, 8);
	g_assert_nonnull(data);
	g_assert_cmpint(memcmp(data, "deadbeef", 8), ==, 0);
}

static void
fu_firmware_ihex_offset_func(void)
{
	const guint8 *data;
	gboolean ret;
	gsize len;
	g_autofree gchar *str = NULL;
	g_autoptr(FuFirmware) firmware = fu_ihex_firmware_new();
	g_autoptr(FuFirmware) firmware_verify = fu_ihex_firmware_new();
	g_autoptr(GBytes) data_bin = NULL;
	g_autoptr(GBytes) data_dummy = NULL;
	g_autoptr(GBytes) data_verify = NULL;
	g_autoptr(GError) error = NULL;

	/* add a 4 byte image in high memory */
	data_dummy = g_bytes_new_static("foo", 4);
	fu_firmware_set_addr(firmware, 0x80000000);
	fu_firmware_set_bytes(firmware, data_dummy);
	data_bin = fu_firmware_write(firmware, &error);
	g_assert_no_error(error);
	g_assert_nonnull(data_bin);
	data = g_bytes_get_data(data_bin, &len);
	str = g_strndup((const gchar *)data, len);
	g_assert_cmpstr(str,
			==,
			":0200000480007A\n"
			":04000000666F6F00B8\n"
			":00000001FF\n");

	/* check we can load it too */
	ret = fu_firmware_parse(firmware_verify, data_bin, FWUPD_INSTALL_FLAG_NO_SEARCH, &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	g_assert_cmpint(fu_firmware_get_addr(firmware_verify), ==, 0x80000000);
	data_verify = fu_firmware_get_bytes(firmware_verify, &error);
	g_assert_no_error(error);
	g_assert_nonnull(data_verify);
	g_assert_cmpint(g_bytes_get_size(data_verify), ==, 0x4);
}

static void
fu_firmware_srec_func(void)
{
	gboolean ret;
	g_autofree gchar *filename_srec = NULL;
	g_autofree gchar *filename_ref = NULL;
	g_autoptr(FuFirmware) firmware = fu_srec_firmware_new();
	g_autoptr(GBytes) data_ref = NULL;
	g_autoptr(GBytes) data_srec = NULL;
	g_autoptr(GBytes) data_bin = NULL;
	g_autoptr(GError) error = NULL;

	filename_srec = g_test_build_filename(G_TEST_DIST, "tests", "firmware.srec", NULL);
	data_srec = fu_bytes_get_contents(filename_srec, &error);
	g_assert_no_error(error);
	g_assert_nonnull(data_srec);
	ret = fu_firmware_parse(firmware, data_srec, FWUPD_INSTALL_FLAG_NO_SEARCH, &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	data_bin = fu_firmware_get_bytes(firmware, &error);
	g_assert_no_error(error);
	g_assert_nonnull(data_bin);
	g_assert_cmpint(g_bytes_get_size(data_bin), ==, 136);

	/* did we match the reference file? */
	filename_ref = g_test_build_filename(G_TEST_DIST, "tests", "firmware.bin", NULL);
	data_ref = fu_bytes_get_contents(filename_ref, &error);
	g_assert_no_error(error);
	g_assert_nonnull(data_ref);
	ret = fu_bytes_compare(data_bin, data_ref, &error);
	g_assert_no_error(error);
	g_assert_true(ret);
}

static void
fu_firmware_fdt_func(void)
{
	gboolean ret;
	guint32 val32 = 0;
	guint64 val64 = 0;
	g_autofree gchar *filename = NULL;
	g_autofree gchar *val = NULL;
	g_autofree gchar *str = NULL;
	g_autoptr(FuFirmware) firmware = fu_fdt_firmware_new();
	g_autoptr(FuFirmware) img1 = NULL;
	g_autoptr(FuFdtImage) img2 = NULL;
	g_autoptr(GBytes) data = NULL;
	g_autoptr(GError) error = NULL;

	filename = g_test_build_filename(G_TEST_DIST, "tests", "fdt.bin", NULL);
	data = fu_bytes_get_contents(filename, &error);
	g_assert_no_error(error);
	g_assert_nonnull(data);
	ret = fu_firmware_parse(firmware, data, FWUPD_INSTALL_FLAG_NO_SEARCH, &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	g_assert_cmpint(fu_fdt_firmware_get_cpuid(FU_FDT_FIRMWARE(firmware)), ==, 0x0);
	str = fu_firmware_to_string(firmware);
	g_debug("%s", str);

	img1 = fu_firmware_get_image_by_id(firmware, NULL, &error);
	g_assert_no_error(error);
	g_assert_nonnull(img1);
	ret = fu_fdt_image_get_attr_str(FU_FDT_IMAGE(img1), "key", &val, &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	g_assert_cmpstr(val, ==, "hello world");

	/* get image, and get the uint32 attr */
	img2 = fu_fdt_firmware_get_image_by_path(FU_FDT_FIRMWARE(firmware),
						 "/images/firmware-1",
						 &error);
	g_assert_no_error(error);
	g_assert_nonnull(img2);
	ret = fu_fdt_image_get_attr_u32(FU_FDT_IMAGE(img2), "key", &val32, &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	g_assert_cmpint(val32, ==, 0x123);

	/* wrong type */
	ret = fu_fdt_image_get_attr_u64(img2, "key", &val64, &error);
	g_assert_error(error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA);
	g_assert_false(ret);
}

static void
fu_firmware_fit_func(void)
{
	gboolean ret;
	g_autofree gchar *filename = NULL;
	g_autofree gchar *str = NULL;
	g_auto(GStrv) val = NULL;
	g_autoptr(FuFdtImage) img1 = NULL;
	g_autoptr(FuFirmware) firmware = fu_fit_firmware_new();
	g_autoptr(GBytes) data = NULL;
	g_autoptr(GError) error = NULL;

	filename = g_test_build_filename(G_TEST_DIST, "tests", "fit.bin", NULL);
	data = fu_bytes_get_contents(filename, &error);
	g_assert_no_error(error);
	g_assert_nonnull(data);
	ret = fu_firmware_parse(firmware, data, FWUPD_INSTALL_FLAG_NO_SEARCH, &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	g_assert_cmpint(fu_fit_firmware_get_timestamp(FU_FIT_FIRMWARE(firmware)), ==, 0x629D4ABD);
	str = fu_firmware_to_string(firmware);
	g_debug("%s", str);

	img1 = fu_fdt_firmware_get_image_by_path(FU_FDT_FIRMWARE(firmware),
						 "/configurations/conf-1",
						 &error);
	g_assert_no_error(error);
	g_assert_nonnull(img1);
	ret = fu_fdt_image_get_attr_strlist(FU_FDT_IMAGE(img1),
					    FU_FIT_FIRMWARE_ATTR_COMPATIBLE,
					    &val,
					    &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	g_assert_nonnull(val);
	g_assert_cmpstr(val[0], ==, "alice");
	g_assert_cmpstr(val[1], ==, "bob");
	g_assert_cmpstr(val[2], ==, "clara");
	g_assert_cmpstr(val[3], ==, NULL);
}

static void
fu_firmware_srec_tokenization_func(void)
{
	FuSrecFirmwareRecord *rcd;
	GPtrArray *records;
	gboolean ret;
	g_autoptr(FuFirmware) firmware = fu_srec_firmware_new();
	g_autoptr(GBytes) data_srec = NULL;
	g_autoptr(GError) error = NULL;
	const gchar *buf = "S3060000001400E5\r\n"
			   "S31000000002281102000000007F0304002C\r\n"
			   "S306000000145095\r\n"
			   "S70500000000FA\r\n";
	data_srec = g_bytes_new_static(buf, strlen(buf));
	g_assert_no_error(error);
	g_assert_nonnull(data_srec);
	ret = fu_firmware_tokenize(firmware, data_srec, FWUPD_INSTALL_FLAG_NONE, &error);
	g_assert_no_error(error);
	g_assert_true(ret);

	records = fu_srec_firmware_get_records(FU_SREC_FIRMWARE(firmware));
	g_assert_nonnull(records);
	g_assert_cmpint(records->len, ==, 4);
	rcd = g_ptr_array_index(records, 2);
	g_assert_nonnull(rcd);
	g_assert_cmpint(rcd->ln, ==, 0x3);
	g_assert_cmpint(rcd->kind, ==, 3);
	g_assert_cmpint(rcd->addr, ==, 0x14);
	g_assert_cmpint(rcd->buf->len, ==, 0x1);
	g_assert_cmpint(rcd->buf->data[0], ==, 0x50);
}

static void
fu_firmware_build_func(void)
{
	gboolean ret;
	g_autofree gchar *str = NULL;
	g_autoptr(FuFirmware) firmware = fu_firmware_new();
	g_autoptr(FuFirmware) img = NULL;
	g_autoptr(GBytes) blob = NULL;
	g_autoptr(GBytes) blob2 = NULL;
	g_autoptr(GError) error = NULL;
	g_autoptr(XbBuilder) builder = xb_builder_new();
	g_autoptr(XbBuilderSource) source = xb_builder_source_new();
	g_autoptr(XbNode) n = NULL;
	g_autoptr(XbSilo) silo = NULL;
	const gchar *buf = "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
			   "<firmware>\n"
			   "  <version>1.2.3</version>\n"
			   "  <firmware>\n"
			   "    <version>4.5.6</version>\n"
			   "    <id>header</id>\n"
			   "    <idx>456</idx>\n"
			   "    <addr>0x456</addr>\n"
			   "    <data>aGVsbG8=</data>\n"
			   "  </firmware>\n"
			   "  <firmware>\n"
			   "    <version>7.8.9</version>\n"
			   "    <id>header</id>\n"
			   "    <idx>789</idx>\n"
			   "    <addr>0x789</addr>\n"
			   "  </firmware>\n"
			   "</firmware>\n";
	blob = g_bytes_new_static(buf, strlen(buf));
	g_assert_no_error(error);
	g_assert_nonnull(blob);

	/* parse XML */
	ret = xb_builder_source_load_bytes(source, blob, XB_BUILDER_SOURCE_FLAG_NONE, &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	xb_builder_import_source(builder, source);
	silo = xb_builder_compile(builder, XB_BUILDER_COMPILE_FLAG_NONE, NULL, &error);
	g_assert_no_error(error);
	g_assert_nonnull(silo);
	n = xb_silo_query_first(silo, "firmware", &error);
	g_assert_no_error(error);
	g_assert_nonnull(n);

	/* build object */
	ret = fu_firmware_build(firmware, n, &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	g_assert_cmpstr(fu_firmware_get_version(firmware), ==, "1.2.3");

	/* verify image */
	img = fu_firmware_get_image_by_id(firmware, "header", &error);
	g_assert_no_error(error);
	g_assert_nonnull(img);
	g_assert_cmpstr(fu_firmware_get_version(img), ==, "4.5.6");
	g_assert_cmpint(fu_firmware_get_idx(img), ==, 456);
	g_assert_cmpint(fu_firmware_get_addr(img), ==, 0x456);
	blob2 = fu_firmware_write(img, &error);
	g_assert_no_error(error);
	g_assert_nonnull(blob2);
	g_assert_cmpint(g_bytes_get_size(blob2), ==, 5);
	str = g_strndup(g_bytes_get_data(blob2, NULL), g_bytes_get_size(blob2));
	g_assert_cmpstr(str, ==, "hello");
}

static gsize
fu_firmware_dfuse_image_get_size(FuFirmware *self)
{
	g_autoptr(GPtrArray) chunks = fu_firmware_get_chunks(self, NULL);
	gsize length = 0;
	for (guint i = 0; i < chunks->len; i++) {
		FuChunk *chk = g_ptr_array_index(chunks, i);
		length += fu_chunk_get_data_sz(chk);
	}
	return length;
}

static gsize
fu_firmware_dfuse_get_size(FuFirmware *firmware)
{
	gsize length = 0;
	g_autoptr(GPtrArray) images = fu_firmware_get_images(firmware);
	for (guint i = 0; i < images->len; i++) {
		FuFirmware *image = g_ptr_array_index(images, i);
		length += fu_firmware_dfuse_image_get_size(image);
	}
	return length;
}

static void
fu_firmware_dfuse_func(void)
{
	gboolean ret;
	g_autofree gchar *filename = NULL;
	g_autoptr(FuFirmware) firmware = fu_dfuse_firmware_new();
	g_autoptr(GBytes) roundtrip_orig = NULL;
	g_autoptr(GBytes) roundtrip = NULL;
	g_autoptr(GError) error = NULL;

	/* load a DfuSe firmware */
	filename = g_test_build_filename(G_TEST_DIST, "tests", "firmware.dfuse", NULL);
	g_assert_nonnull(filename);
	roundtrip_orig = fu_bytes_get_contents(filename, &error);
	g_assert_no_error(error);
	g_assert_nonnull(roundtrip_orig);
	ret = fu_firmware_parse(firmware, roundtrip_orig, FWUPD_INSTALL_FLAG_NO_SEARCH, &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	g_assert_cmpint(fu_dfu_firmware_get_vid(FU_DFU_FIRMWARE(firmware)), ==, 0x1234);
	g_assert_cmpint(fu_dfu_firmware_get_pid(FU_DFU_FIRMWARE(firmware)), ==, 0x5678);
	g_assert_cmpint(fu_dfu_firmware_get_release(FU_DFU_FIRMWARE(firmware)), ==, 0x8642);
	g_assert_cmpint(fu_firmware_dfuse_get_size(firmware), ==, 0x21);

	/* can we roundtrip without losing data */
	roundtrip = fu_firmware_write(firmware, &error);
	g_assert_no_error(error);
	g_assert_nonnull(roundtrip);
	ret = fu_bytes_compare(roundtrip, roundtrip_orig, &error);
	g_assert_no_error(error);
	g_assert_true(ret);
}

static void
fu_firmware_fmap_func(void)
{
	gboolean ret;
	g_autofree gchar *filename = NULL;
	g_autofree gchar *img_str = NULL;
	g_autoptr(FuFirmware) firmware = fu_fmap_firmware_new();
	g_autoptr(FuFirmware) img = NULL;
	g_autoptr(GBytes) img_blob = NULL;
	g_autoptr(GBytes) roundtrip = NULL;
	g_autoptr(GBytes) roundtrip_orig = NULL;
	g_autoptr(GError) error = NULL;
	g_autoptr(GPtrArray) images = NULL;

#ifndef HAVE_MEMMEM
	g_test_skip("no memmem()");
	return;
#endif

	/* load firmware */
	filename = g_test_build_filename(G_TEST_DIST, "tests", "fmap-offset.bin", NULL);
	g_assert_nonnull(filename);
	roundtrip_orig = fu_bytes_get_contents(filename, &error);
	g_assert_no_error(error);
	g_assert_nonnull(roundtrip_orig);
	ret = fu_firmware_parse(firmware, roundtrip_orig, FWUPD_INSTALL_FLAG_NONE, &error);
	g_assert_no_error(error);
	g_assert_true(ret);

	/* check image count */
	images = fu_firmware_get_images(firmware);
	g_assert_cmpint(images->len, ==, 2);

	/* get a specific image */
	img = fu_firmware_get_image_by_id(firmware, "FMAP", &error);
	g_assert_no_error(error);
	g_assert_nonnull(img);
	img_blob = fu_firmware_get_bytes(img, &error);
	g_assert_no_error(error);
	g_assert_nonnull(img_blob);
	g_assert_cmpint(g_bytes_get_size(img_blob), ==, 0xb);
	img_str = g_strndup(g_bytes_get_data(img_blob, NULL), g_bytes_get_size(img_blob));
	g_assert_cmpstr(img_str, ==, "hello world");

	/* can we roundtrip without losing data */
	roundtrip = fu_firmware_write(firmware, &error);
	g_assert_no_error(error);
	g_assert_nonnull(roundtrip);
	ret = fu_bytes_compare(roundtrip, roundtrip_orig, &error);
	g_assert_no_error(error);
	g_assert_true(ret);
}

static void
fu_firmware_new_from_gtypes_func(void)
{
	g_autofree gchar *fn = NULL;
	g_autoptr(FuFirmware) firmware1 = NULL;
	g_autoptr(FuFirmware) firmware2 = NULL;
	g_autoptr(FuFirmware) firmware3 = NULL;
	g_autoptr(GBytes) blob = NULL;
	g_autoptr(GError) error = NULL;

	fn = g_test_build_filename(G_TEST_DIST, "tests", "firmware.dfu", NULL);
	blob = fu_bytes_get_contents(fn, &error);
	g_assert_no_error(error);
	g_assert_nonnull(blob);

	/* dfu -> FuDfuFirmware */
	firmware1 = fu_firmware_new_from_gtypes(blob,
						FWUPD_INSTALL_FLAG_NONE,
						&error,
						FU_TYPE_SREC_FIRMWARE,
						FU_TYPE_DFUSE_FIRMWARE,
						FU_TYPE_DFU_FIRMWARE,
						G_TYPE_INVALID);
	g_assert_no_error(error);
	g_assert_nonnull(firmware1);
	g_assert_cmpstr(G_OBJECT_TYPE_NAME(firmware1), ==, "FuDfuFirmware");

	/* dfu -> FuFirmware */
	firmware2 = fu_firmware_new_from_gtypes(blob,
						FWUPD_INSTALL_FLAG_NONE,
						&error,
						FU_TYPE_SREC_FIRMWARE,
						FU_TYPE_FIRMWARE,
						G_TYPE_INVALID);
	g_assert_no_error(error);
	g_assert_nonnull(firmware2);
	g_assert_cmpstr(G_OBJECT_TYPE_NAME(firmware2), ==, "FuFirmware");

	/* dfu -> error */
	firmware3 = fu_firmware_new_from_gtypes(blob,
						FWUPD_INSTALL_FLAG_NONE,
						&error,
						FU_TYPE_SREC_FIRMWARE,
						G_TYPE_INVALID);
	g_assert_error(error, FWUPD_ERROR, FWUPD_ERROR_INVALID_FILE);
	g_assert_null(firmware3);
}

static void
fu_firmware_linear_func(void)
{
	gboolean ret;
	g_autoptr(FuFirmware) firmware1 = fu_linear_firmware_new(FU_TYPE_OPROM_FIRMWARE);
	g_autoptr(FuFirmware) firmware2 = fu_linear_firmware_new(FU_TYPE_OPROM_FIRMWARE);
	g_autoptr(GBytes) blob1 = g_bytes_new_static("XXXX", 4);
	g_autoptr(GBytes) blob2 = g_bytes_new_static("HELO", 4);
	g_autoptr(GBytes) blob3 = NULL;
	g_autoptr(FuFirmware) img1 = fu_oprom_firmware_new();
	g_autoptr(FuFirmware) img2 = fu_oprom_firmware_new();
	g_autoptr(GError) error = NULL;
	g_autoptr(GPtrArray) imgs = NULL;
	g_autofree gchar *str = NULL;

	/* add images then parse */
	fu_firmware_set_bytes(img1, blob1);
	fu_firmware_add_image(firmware1, img1);
	fu_firmware_set_bytes(img2, blob2);
	fu_firmware_add_image(firmware1, img2);
	blob3 = fu_firmware_write(firmware1, &error);
	g_assert_no_error(error);
	g_assert_nonnull(blob3);
	g_assert_cmpint(g_bytes_get_size(blob3), ==, 1024);

	/* parse them back */
	ret = fu_firmware_parse(firmware2, blob3, FWUPD_INSTALL_FLAG_NO_SEARCH, &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	str = fu_firmware_to_string(firmware2);
	g_debug("\n%s", str);

	/* verify we got both images */
	imgs = fu_firmware_get_images(firmware2);
	g_assert_cmpint(imgs->len, ==, 2);
}

static void
fu_firmware_dfu_func(void)
{
	gboolean ret;
	g_autofree gchar *filename_dfu = NULL;
	g_autofree gchar *filename_ref = NULL;
	g_autoptr(FuFirmware) firmware = fu_dfu_firmware_new();
	g_autoptr(GBytes) data_ref = NULL;
	g_autoptr(GBytes) data_dfu = NULL;
	g_autoptr(GBytes) data_bin = NULL;
	g_autoptr(GError) error = NULL;

	filename_dfu = g_test_build_filename(G_TEST_DIST, "tests", "firmware.dfu", NULL);
	data_dfu = fu_bytes_get_contents(filename_dfu, &error);
	g_assert_no_error(error);
	g_assert_nonnull(data_dfu);
	ret = fu_firmware_parse(firmware, data_dfu, FWUPD_INSTALL_FLAG_NO_SEARCH, &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	g_assert_cmpint(fu_dfu_firmware_get_vid(FU_DFU_FIRMWARE(firmware)), ==, 0x1234);
	g_assert_cmpint(fu_dfu_firmware_get_pid(FU_DFU_FIRMWARE(firmware)), ==, 0x4321);
	g_assert_cmpint(fu_dfu_firmware_get_release(FU_DFU_FIRMWARE(firmware)), ==, 0xdead);
	data_bin = fu_firmware_get_bytes(firmware, &error);
	g_assert_no_error(error);
	g_assert_nonnull(data_bin);
	g_assert_cmpint(g_bytes_get_size(data_bin), ==, 136);

	/* did we match the reference file? */
	filename_ref = g_test_build_filename(G_TEST_DIST, "tests", "firmware.bin", NULL);
	data_ref = fu_bytes_get_contents(filename_ref, &error);
	g_assert_no_error(error);
	g_assert_nonnull(data_ref);
	ret = fu_bytes_compare(data_bin, data_ref, &error);
	g_assert_no_error(error);
	g_assert_true(ret);
}

static void
fu_firmware_ifwi_cpd_func(void)
{
	gboolean ret;
	g_autofree gchar *filename_ifwi_cpd = NULL;
	g_autoptr(FuFirmware) firmware = fu_ifwi_cpd_firmware_new();
	g_autoptr(FuFirmware) img1 = NULL;
	g_autoptr(FuFirmware) img2 = NULL;
	g_autoptr(GBytes) data_bin = NULL;
	g_autoptr(GBytes) data_ifwi_cpd = NULL;
	g_autoptr(GError) error = NULL;

	filename_ifwi_cpd = g_test_build_filename(G_TEST_DIST, "tests", "ifwi-cpd.bin", NULL);
	data_ifwi_cpd = fu_bytes_get_contents(filename_ifwi_cpd, &error);
	g_assert_no_error(error);
	g_assert_nonnull(data_ifwi_cpd);
	ret = fu_firmware_parse(firmware, data_ifwi_cpd, FWUPD_INSTALL_FLAG_NO_SEARCH, &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	g_assert_cmpint(fu_firmware_get_idx(firmware), ==, 0x1234);
	data_bin = fu_firmware_write(firmware, &error);
	g_assert_no_error(error);
	g_assert_nonnull(data_bin);
	g_assert_cmpint(g_bytes_get_size(data_bin), ==, 90);

	img1 = fu_firmware_get_image_by_id(firmware, "one", &error);
	g_assert_no_error(error);
	g_assert_nonnull(img1);
	g_assert_cmpint(fu_firmware_get_offset(img1), ==, 68);
	g_assert_cmpint(fu_firmware_get_size(img1), ==, 11);

	img2 = fu_firmware_get_image_by_id(firmware, "two", &error);
	g_assert_no_error(error);
	g_assert_nonnull(img2);
	g_assert_cmpint(fu_firmware_get_offset(img2), ==, 79);
	g_assert_cmpint(fu_firmware_get_size(img2), ==, 11);
}

static void
fu_firmware_ifwi_fpt_func(void)
{
	gboolean ret;
	g_autofree gchar *filename_ifwi_fpt = NULL;
	g_autoptr(FuFirmware) firmware = fu_ifwi_fpt_firmware_new();
	g_autoptr(FuFirmware) img1 = NULL;
	g_autoptr(FuFirmware) img2 = NULL;
	g_autoptr(GBytes) data_bin = NULL;
	g_autoptr(GBytes) data_ifwi_fpt = NULL;
	g_autoptr(GError) error = NULL;

	filename_ifwi_fpt = g_test_build_filename(G_TEST_DIST, "tests", "ifwi-fpt.bin", NULL);
	data_ifwi_fpt = fu_bytes_get_contents(filename_ifwi_fpt, &error);
	g_assert_no_error(error);
	g_assert_nonnull(data_ifwi_fpt);
	ret = fu_firmware_parse(firmware, data_ifwi_fpt, FWUPD_INSTALL_FLAG_NO_SEARCH, &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	data_bin = fu_firmware_write(firmware, &error);
	g_assert_no_error(error);
	g_assert_nonnull(data_bin);
	g_assert_cmpint(g_bytes_get_size(data_bin), ==, 118);

	img1 = fu_firmware_get_image_by_idx(firmware, 0x4f464e49, &error);
	g_assert_no_error(error);
	g_assert_nonnull(img1);
	g_assert_cmpint(fu_firmware_get_offset(img1), ==, 96);
	g_assert_cmpint(fu_firmware_get_size(img1), ==, 11);

	img2 = fu_firmware_get_image_by_idx(firmware, 0x4d495746, &error);
	g_assert_no_error(error);
	g_assert_nonnull(img2);
	g_assert_cmpint(fu_firmware_get_offset(img2), ==, 107);
	g_assert_cmpint(fu_firmware_get_size(img2), ==, 11);
}

static void
fu_firmware_oprom_func(void)
{
	gboolean ret;
	g_autofree gchar *filename_oprom = NULL;
	g_autoptr(FuFirmware) firmware = fu_oprom_firmware_new();
	g_autoptr(FuFirmware) img1 = NULL;
	g_autoptr(GBytes) data_bin = NULL;
	g_autoptr(GBytes) data_oprom = NULL;
	g_autoptr(GError) error = NULL;

	filename_oprom = g_test_build_filename(G_TEST_DIST, "tests", "oprom.bin", NULL);
	data_oprom = fu_bytes_get_contents(filename_oprom, &error);
	g_assert_no_error(error);
	g_assert_nonnull(data_oprom);
	ret = fu_firmware_parse(firmware, data_oprom, FWUPD_INSTALL_FLAG_NO_SEARCH, &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	g_assert_cmpint(fu_firmware_get_idx(firmware), ==, 0x1);
	data_bin = fu_firmware_write(firmware, &error);
	g_assert_no_error(error);
	g_assert_nonnull(data_bin);
	g_assert_cmpint(g_bytes_get_size(data_bin), ==, 1024);

	img1 = fu_firmware_get_image_by_id(firmware, "cpd", &error);
	g_assert_no_error(error);
	g_assert_nonnull(img1);
	g_assert_cmpint(fu_firmware_get_offset(img1), ==, 512);
	g_assert_cmpint(fu_firmware_get_size(img1), ==, 512);
}

static void
fu_firmware_dfu_patch_func(void)
{
	gboolean ret;
	g_autofree gchar *csum = NULL;
	g_autofree gchar *filename_dfu = NULL;
	g_autoptr(FuFirmware) firmware = fu_dfu_firmware_new();
	g_autoptr(GBytes) data_dfu = NULL;
	g_autoptr(GBytes) data_new = NULL;
	g_autoptr(GBytes) data_patch0 = g_bytes_new_static("XXXX", 4);
	g_autoptr(GBytes) data_patch1 = g_bytes_new_static("HELO", 4);
	g_autoptr(GError) error = NULL;

	filename_dfu = g_test_build_filename(G_TEST_DIST, "tests", "firmware.dfu", NULL);
	data_dfu = fu_bytes_get_contents(filename_dfu, &error);
	g_assert_no_error(error);
	g_assert_nonnull(data_dfu);
	ret = fu_firmware_parse(firmware, data_dfu, FWUPD_INSTALL_FLAG_NO_SEARCH, &error);
	g_assert_no_error(error);
	g_assert_true(ret);

	/* add a couple of patches */
	fu_firmware_add_patch(firmware, 0x0, data_patch0);
	fu_firmware_add_patch(firmware, 0x0, data_patch1);
	fu_firmware_add_patch(firmware, 136 - 4, data_patch1);

	data_new = fu_firmware_write(firmware, &error);
	g_assert_no_error(error);
	g_assert_nonnull(data_new);
	fu_dump_full(G_LOG_DOMAIN,
		     "patch",
		     g_bytes_get_data(data_new, NULL),
		     g_bytes_get_size(data_new),
		     20,
		     FU_DUMP_FLAGS_SHOW_ASCII | FU_DUMP_FLAGS_SHOW_ADDRESSES);
	csum = g_compute_checksum_for_bytes(G_CHECKSUM_SHA1, data_new);
	g_assert_cmpstr(csum, ==, "0722727426092ac564861d1a11697182017be83f");
}

static void
fu_firmware_func(void)
{
	gboolean ret;
	g_autoptr(FuFirmware) firmware = fu_firmware_new();
	g_autoptr(FuFirmware) img1 = fu_firmware_new();
	g_autoptr(FuFirmware) img2 = fu_firmware_new();
	g_autoptr(FuFirmware) img_id = NULL;
	g_autoptr(FuFirmware) img_idx = NULL;
	g_autoptr(GError) error = NULL;
	g_autoptr(GPtrArray) images = NULL;
	g_autofree gchar *str = NULL;

	fu_firmware_set_addr(img1, 0x200);
	fu_firmware_set_idx(img1, 13);
	fu_firmware_set_id(img1, "primary");
	fu_firmware_set_filename(img1, "BIOS.bin");
	fu_firmware_add_image(firmware, img1);
	fu_firmware_set_addr(img2, 0x400);
	fu_firmware_set_idx(img2, 23);
	fu_firmware_set_id(img2, "secondary");
	fu_firmware_add_image(firmware, img2);

	img_id = fu_firmware_get_image_by_id(firmware, "NotGoingToExist", &error);
	g_assert_error(error, FWUPD_ERROR, FWUPD_ERROR_NOT_FOUND);
	g_assert_null(img_id);
	g_clear_error(&error);
	img_id = fu_firmware_get_image_by_id(firmware, "primary", &error);
	g_assert_no_error(error);
	g_assert_nonnull(img_id);
	g_assert_cmpint(fu_firmware_get_addr(img_id), ==, 0x200);
	g_assert_cmpint(fu_firmware_get_idx(img_id), ==, 13);
	g_assert_cmpstr(fu_firmware_get_id(img_id), ==, "primary");

	img_idx = fu_firmware_get_image_by_idx(firmware, 123456, &error);
	g_assert_error(error, FWUPD_ERROR, FWUPD_ERROR_NOT_FOUND);
	g_assert_null(img_idx);
	g_clear_error(&error);
	img_idx = fu_firmware_get_image_by_idx(firmware, 23, &error);
	g_assert_no_error(error);
	g_assert_nonnull(img_idx);
	g_assert_cmpint(fu_firmware_get_addr(img_idx), ==, 0x400);
	g_assert_cmpint(fu_firmware_get_idx(img_idx), ==, 23);
	g_assert_cmpstr(fu_firmware_get_id(img_idx), ==, "secondary");

	str = fu_firmware_to_string(firmware);
	g_assert_cmpstr(str,
			==,
			"<firmware>\n"
			"  <firmware>\n"
			"    <id>primary</id>\n"
			"    <idx>0xd</idx>\n"
			"    <addr>0x200</addr>\n"
			"    <filename>BIOS.bin</filename>\n"
			"  </firmware>\n"
			"  <firmware>\n"
			"    <id>secondary</id>\n"
			"    <idx>0x17</idx>\n"
			"    <addr>0x400</addr>\n"
			"  </firmware>\n"
			"</firmware>\n");

	ret = fu_firmware_remove_image_by_idx(firmware, 0xd, &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	ret = fu_firmware_remove_image_by_id(firmware, "secondary", &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	images = fu_firmware_get_images(firmware);
	g_assert_nonnull(images);
	g_assert_cmpint(images->len, ==, 0);
	ret = fu_firmware_remove_image_by_id(firmware, "NOTGOINGTOEXIST", &error);
	g_assert_error(error, FWUPD_ERROR, FWUPD_ERROR_NOT_FOUND);
	g_assert_false(ret);
}

static void
fu_firmware_common_func(void)
{
	gboolean ret;
	guint8 value = 0;
	g_autoptr(GError) error = NULL;

	ret = fu_firmware_strparse_uint8_safe("ff00XX", 6, 0, &value, &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	g_assert_cmpint(value, ==, 0xFF);

	ret = fu_firmware_strparse_uint8_safe("ff00XX", 6, 2, &value, &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	g_assert_cmpint(value, ==, 0x00);

	ret = fu_firmware_strparse_uint8_safe("ff00XX", 6, 4, &value, &error);
	g_assert_error(error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA);
	g_assert_false(ret);
}

static void
fu_firmware_dedupe_func(void)
{
	g_autoptr(FuFirmware) firmware = fu_firmware_new();
	g_autoptr(FuFirmware) img1 = fu_firmware_new();
	g_autoptr(FuFirmware) img1_old = fu_firmware_new();
	g_autoptr(FuFirmware) img2 = fu_firmware_new();
	g_autoptr(FuFirmware) img2_old = fu_firmware_new();
	g_autoptr(FuFirmware) img_id = NULL;
	g_autoptr(FuFirmware) img_idx = NULL;
	g_autoptr(GError) error = NULL;

	fu_firmware_add_flag(firmware, FU_FIRMWARE_FLAG_DEDUPE_ID);
	fu_firmware_add_flag(firmware, FU_FIRMWARE_FLAG_DEDUPE_IDX);

	fu_firmware_set_idx(img1_old, 13);
	fu_firmware_set_id(img1_old, "DAVE");
	fu_firmware_add_image(firmware, img1_old);
	g_assert_true(fu_firmware_get_parent(img1_old) == firmware);

	fu_firmware_set_idx(img1, 13);
	fu_firmware_set_id(img1, "primary");
	fu_firmware_add_image(firmware, img1);

	fu_firmware_set_idx(img2_old, 123456);
	fu_firmware_set_id(img2_old, "secondary");
	fu_firmware_add_image(firmware, img2_old);

	fu_firmware_set_idx(img2, 23);
	fu_firmware_set_id(img2, "secondary");
	fu_firmware_add_image(firmware, img2);

	img_id = fu_firmware_get_image_by_id(firmware, "primary", &error);
	g_assert_no_error(error);
	g_assert_nonnull(img_id);
	g_assert_cmpint(fu_firmware_get_idx(img_id), ==, 13);
	g_assert_cmpstr(fu_firmware_get_id(img_id), ==, "primary");

	img_idx = fu_firmware_get_image_by_idx(firmware, 23, &error);
	g_assert_no_error(error);
	g_assert_nonnull(img_idx);
	g_assert_cmpint(fu_firmware_get_idx(img_idx), ==, 23);
	g_assert_cmpstr(fu_firmware_get_id(img_idx), ==, "secondary");
}

static void
fu_efivar_func(void)
{
	gboolean ret;
	gsize sz = 0;
	guint32 attr = 0;
	guint64 total;
	g_autofree gchar *sysfsfwdir = NULL;
	g_autofree guint8 *data = NULL;
	g_autoptr(GError) error = NULL;
	g_autoptr(GPtrArray) names = NULL;

#ifndef __linux__
	g_test_skip("only works on Linux");
	return;
#endif

	/* these tests will write */
	sysfsfwdir = g_test_build_filename(G_TEST_BUILT, "tests", NULL);
	(void)g_setenv("FWUPD_SYSFSFWDIR", sysfsfwdir, TRUE);

	/* check supported */
	ret = fu_efivar_supported(&error);
	g_assert_no_error(error);
	g_assert_true(ret);

	/* check we can get the space used */
	total = fu_efivar_space_used(&error);
	g_assert_no_error(error);
	g_assert_cmpint(total, >=, 0x2000);

	/* check existing keys */
	g_assert_false(fu_efivar_exists(FU_EFIVAR_GUID_EFI_GLOBAL, "NotGoingToExist"));
	g_assert_true(fu_efivar_exists(FU_EFIVAR_GUID_EFI_GLOBAL, "SecureBoot"));

	/* list a few keys */
	names = fu_efivar_get_names(FU_EFIVAR_GUID_EFI_GLOBAL, &error);
	g_assert_no_error(error);
	g_assert_nonnull(names);
	g_assert_cmpint(names->len, ==, 2);

	/* write and read a key */
	ret = fu_efivar_set_data(FU_EFIVAR_GUID_EFI_GLOBAL,
				 "Test",
				 (guint8 *)"1",
				 1,
				 FU_EFIVAR_ATTR_NON_VOLATILE | FU_EFIVAR_ATTR_RUNTIME_ACCESS,
				 &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	ret = fu_efivar_get_data(FU_EFIVAR_GUID_EFI_GLOBAL, "Test", &data, &sz, &attr, &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	g_assert_cmpint(sz, ==, 1);
	g_assert_cmpint(attr, ==, FU_EFIVAR_ATTR_NON_VOLATILE | FU_EFIVAR_ATTR_RUNTIME_ACCESS);
	g_assert_cmpint(data[0], ==, '1');

	/* delete single key */
	ret = fu_efivar_delete(FU_EFIVAR_GUID_EFI_GLOBAL, "Test", &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	g_assert_false(fu_efivar_exists(FU_EFIVAR_GUID_EFI_GLOBAL, "Test"));

	/* delete multiple keys */
	ret = fu_efivar_set_data(FU_EFIVAR_GUID_EFI_GLOBAL, "Test1", (guint8 *)"1", 1, 0, &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	ret = fu_efivar_set_data(FU_EFIVAR_GUID_EFI_GLOBAL, "Test2", (guint8 *)"1", 1, 0, &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	ret = fu_efivar_delete_with_glob(FU_EFIVAR_GUID_EFI_GLOBAL, "Test*", &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	g_assert_false(fu_efivar_exists(FU_EFIVAR_GUID_EFI_GLOBAL, "Test1"));
	g_assert_false(fu_efivar_exists(FU_EFIVAR_GUID_EFI_GLOBAL, "Test2"));

	/* read a key that doesn't exist */
	ret = fu_efivar_get_data(FU_EFIVAR_GUID_EFI_GLOBAL,
				 "NotGoingToExist",
				 NULL,
				 NULL,
				 NULL,
				 &error);
	g_assert_error(error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND);
	g_assert_false(ret);
}

typedef struct {
	guint cnt_success;
	guint cnt_failed;
} FuDeviceRetryHelper;

static gboolean
fu_device_retry_success(FuDevice *device, gpointer user_data, GError **error)
{
	FuDeviceRetryHelper *helper = (FuDeviceRetryHelper *)user_data;
	helper->cnt_success++;
	return TRUE;
}

static gboolean
fu_device_retry_failed(FuDevice *device, gpointer user_data, GError **error)
{
	FuDeviceRetryHelper *helper = (FuDeviceRetryHelper *)user_data;
	helper->cnt_failed++;
	g_set_error_literal(error, FWUPD_ERROR, FWUPD_ERROR_INTERNAL, "failed");
	return FALSE;
}

static gboolean
fu_device_retry_success_3rd_try(FuDevice *device, gpointer user_data, GError **error)
{
	FuDeviceRetryHelper *helper = (FuDeviceRetryHelper *)user_data;
	if (helper->cnt_failed == 2) {
		helper->cnt_success++;
		return TRUE;
	}
	helper->cnt_failed++;
	g_set_error_literal(error, FWUPD_ERROR, FWUPD_ERROR_INTERNAL, "failed");
	return FALSE;
}

static void
fu_device_retry_success_func(void)
{
	gboolean ret;
	g_autoptr(FuDevice) device = fu_device_new(NULL);
	g_autoptr(GError) error = NULL;
	FuDeviceRetryHelper helper = {
	    .cnt_success = 0,
	    .cnt_failed = 0,
	};
	fu_device_retry_add_recovery(device,
				     FWUPD_ERROR,
				     FWUPD_ERROR_INTERNAL,
				     fu_device_retry_failed);
	ret = fu_device_retry(device, fu_device_retry_success, 3, &helper, &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	g_assert_cmpint(helper.cnt_success, ==, 1);
	g_assert_cmpint(helper.cnt_failed, ==, 0);
}

static void
fu_device_retry_failed_func(void)
{
	gboolean ret;
	g_autoptr(FuDevice) device = fu_device_new(NULL);
	g_autoptr(GError) error = NULL;
	FuDeviceRetryHelper helper = {
	    .cnt_success = 0,
	    .cnt_failed = 0,
	};
	fu_device_retry_add_recovery(device,
				     FWUPD_ERROR,
				     FWUPD_ERROR_INTERNAL,
				     fu_device_retry_success);
	ret = fu_device_retry(device, fu_device_retry_failed, 3, &helper, &error);
	g_assert_error(error, FWUPD_ERROR, FWUPD_ERROR_INTERNAL);
	g_assert_true(!ret);
	g_assert_cmpint(helper.cnt_success, ==, 2); /* do not reset for the last failure */
	g_assert_cmpint(helper.cnt_failed, ==, 3);
}

static void
fu_device_retry_hardware_func(void)
{
	gboolean ret;
	g_autoptr(FuDevice) device = fu_device_new(NULL);
	g_autoptr(GError) error = NULL;
	FuDeviceRetryHelper helper = {
	    .cnt_success = 0,
	    .cnt_failed = 0,
	};
	ret = fu_device_retry(device, fu_device_retry_success_3rd_try, 3, &helper, &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	g_assert_cmpint(helper.cnt_success, ==, 1);
	g_assert_cmpint(helper.cnt_failed, ==, 2);
}

static void
fu_bios_settings_load_func(void)
{
	gboolean ret;
	gint integer;
	const gchar *tmp;
	GPtrArray *values;
	FwupdBiosSetting *setting;
	FwupdBiosSettingKind kind;
	g_autofree gchar *test_dir = NULL;
	g_autoptr(FuContext) ctx = fu_context_new();
	g_autoptr(GError) error = NULL;
	g_autoptr(FuBiosSettings) p620_settings = NULL;
	g_autoptr(FuBiosSettings) p14s_settings = NULL;
	g_autoptr(FuBiosSettings) xp29310_settings = NULL;
	g_autoptr(GPtrArray) p14s_items = NULL;
	g_autoptr(GPtrArray) p620_items = NULL;
	g_autoptr(GPtrArray) xps9310_items = NULL;

	/* load BIOS settings from a Lenovo P620 (with thinklmi driver problems) */
	test_dir = g_test_build_filename(G_TEST_DIST, "tests", "bios-attrs", "lenovo-p620", NULL);
	(void)g_setenv("FWUPD_SYSFSFWATTRIBDIR", test_dir, TRUE);

	g_test_expect_message("FuBiosSettings", G_LOG_LEVEL_WARNING, "*BUG*");
	ret = fu_context_reload_bios_settings(ctx, &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	g_test_assert_expected_messages();

	p620_settings = fu_context_get_bios_settings(ctx);
	p620_items = fu_bios_settings_get_all(p620_settings);
	g_assert_cmpint(p620_items->len, ==, 128);

	/* make sure nothing pending */
	ret = fu_context_get_bios_setting_pending_reboot(ctx);
	g_assert_false(ret);

	/* check a BIOS setting reads from kernel as expected by fwupd today */
	setting = fu_context_get_bios_setting(ctx, "com.thinklmi.AMDMemoryGuard");
	g_assert_nonnull(setting);
	tmp = fwupd_bios_setting_get_name(setting);
	g_assert_cmpstr(tmp, ==, "AMDMemoryGuard");
	tmp = fwupd_bios_setting_get_description(setting);
	g_assert_cmpstr(tmp, ==, "AMDMemoryGuard");
	tmp = fwupd_bios_setting_get_current_value(setting);
	g_assert_cmpstr(tmp, ==, "Disable");
	values = fwupd_bios_setting_get_possible_values(setting);
	for (guint i = 0; i < values->len; i++) {
		const gchar *possible = g_ptr_array_index(values, i);
		if (i == 0)
			g_assert_cmpstr(possible, ==, "Disable");
		if (i == 1)
			g_assert_cmpstr(possible, ==, "Enable");
	}

	/* try to read an BIOS setting known to have ][Status] to make sure we worked
	 * around the thinklmi bug sufficiently
	 */
	setting = fu_context_get_bios_setting(ctx, "com.thinklmi.StartupSequence");
	g_assert_nonnull(setting);
	tmp = fwupd_bios_setting_get_current_value(setting);
	g_assert_cmpstr(tmp, ==, "Primary");
	values = fwupd_bios_setting_get_possible_values(setting);
	for (guint i = 0; i < values->len; i++) {
		const gchar *possible = g_ptr_array_index(values, i);
		if (i == 0)
			g_assert_cmpstr(possible, ==, "Primary");
		if (i == 1)
			g_assert_cmpstr(possible, ==, "Automatic");
	}

	/* check no BIOS settings have [Status in them */
	for (guint i = 0; i < p620_items->len; i++) {
		setting = g_ptr_array_index(p620_items, i);
		tmp = fwupd_bios_setting_get_current_value(setting);
		g_debug("%s", tmp);
		g_assert_null(g_strrstr(tmp, "[Status"));
	}

	g_free(test_dir);

	/* load BIOS settings from a Lenovo P14s Gen1 */
	test_dir =
	    g_test_build_filename(G_TEST_DIST, "tests", "bios-attrs", "lenovo-p14s-gen1", NULL);
	(void)g_setenv("FWUPD_SYSFSFWATTRIBDIR", test_dir, TRUE);
	ret = fu_context_reload_bios_settings(ctx, &error);
	g_assert_no_error(error);
	g_assert_true(ret);

	p14s_settings = fu_context_get_bios_settings(ctx);
	p14s_items = fu_bios_settings_get_all(p14s_settings);
	g_assert_cmpint(p14s_items->len, ==, 75);

	/* reboot should be pending on this one */
	ret = fu_context_get_bios_setting_pending_reboot(ctx);
	g_assert_true(ret);

	/* look for an enumeration BIOS setting with a space */
	setting = fu_context_get_bios_setting(ctx, "com.thinklmi.SleepState");
	g_assert_nonnull(setting);
	tmp = fwupd_bios_setting_get_name(setting);
	g_assert_cmpstr(tmp, ==, "SleepState");
	tmp = fwupd_bios_setting_get_description(setting);
	g_assert_cmpstr(tmp, ==, "SleepState");
	values = fwupd_bios_setting_get_possible_values(setting);
	for (guint i = 0; i < values->len; i++) {
		const gchar *possible = g_ptr_array_index(values, i);
		if (i == 0)
			g_assert_cmpstr(possible, ==, "Linux");
		if (i == 1)
			g_assert_cmpstr(possible, ==, "Windows 10");
	}

	/* make sure we defaulted UEFI Secure boot to read only if enabled */
	setting = fu_context_get_bios_setting(ctx, "com.thinklmi.SecureBoot");
	g_assert_nonnull(setting);
	ret = fwupd_bios_setting_get_read_only(setting);
	g_assert_true(ret);

	g_free(test_dir);

	/* load BIOS settings from a Dell XPS 9310 */
	test_dir =
	    g_test_build_filename(G_TEST_DIST, "tests", "bios-attrs", "dell-xps13-9310", NULL);
	(void)g_setenv("FWUPD_SYSFSFWATTRIBDIR", test_dir, TRUE);
	ret = fu_context_reload_bios_settings(ctx, &error);
	g_assert_no_error(error);
	g_assert_true(ret);

	xp29310_settings = fu_context_get_bios_settings(ctx);
	xps9310_items = fu_bios_settings_get_all(xp29310_settings);
	g_assert_cmpint(xps9310_items->len, ==, 109);

	/* make sure that we DIDN'T parse reset_bios setting */
	setting = fu_context_get_bios_setting(ctx, FWUPD_BIOS_SETTING_RESET_BIOS);
	g_assert_null(setting);

	/* look at a integer BIOS setting */
	setting = fu_context_get_bios_setting(ctx, "com.dell-wmi-sysman.CustomChargeStop");
	g_assert_nonnull(setting);
	kind = fwupd_bios_setting_get_kind(setting);
	g_assert_cmpint(kind, ==, FWUPD_BIOS_SETTING_KIND_INTEGER);
	integer = fwupd_bios_setting_get_lower_bound(setting);
	g_assert_cmpint(integer, ==, 55);
	integer = fwupd_bios_setting_get_upper_bound(setting);
	g_assert_cmpint(integer, ==, 100);
	integer = fwupd_bios_setting_get_scalar_increment(setting);
	g_assert_cmpint(integer, ==, 1);

	/* look at a string BIOS setting */
	setting = fu_context_get_bios_setting(ctx, "com.dell-wmi-sysman.Asset");
	g_assert_nonnull(setting);
	integer = fwupd_bios_setting_get_lower_bound(setting);
	g_assert_cmpint(integer, ==, 1);
	integer = fwupd_bios_setting_get_upper_bound(setting);
	g_assert_cmpint(integer, ==, 64);
	tmp = fwupd_bios_setting_get_description(setting);
	g_assert_cmpstr(tmp, ==, "Asset Tag");

	/* look at a enumeration BIOS setting */
	setting = fu_context_get_bios_setting(ctx, "com.dell-wmi-sysman.BiosRcvrFrmHdd");
	g_assert_nonnull(setting);
	kind = fwupd_bios_setting_get_kind(setting);
	g_assert_cmpint(kind, ==, FWUPD_BIOS_SETTING_KIND_ENUMERATION);
	values = fwupd_bios_setting_get_possible_values(setting);
	for (guint i = 0; i < values->len; i++) {
		const gchar *possible = g_ptr_array_index(values, i);
		if (i == 0)
			g_assert_cmpstr(possible, ==, "Disabled");
		if (i == 1)
			g_assert_cmpstr(possible, ==, "Enabled");
	}

	/* make sure we defaulted UEFI Secure boot to read only if enabled */
	setting = fu_context_get_bios_setting(ctx, "com.dell-wmi-sysman.SecureBoot");
	g_assert_nonnull(setting);
	ret = fwupd_bios_setting_get_read_only(setting);
	g_assert_true(ret);
}

static void
fu_security_attrs_hsi_func(void)
{
	g_autofree gchar *hsi1 = NULL;
	g_autofree gchar *hsi2 = NULL;
	g_autofree gchar *hsi3 = NULL;
	g_autofree gchar *hsi4 = NULL;
	g_autofree gchar *hsi5 = NULL;
	g_autofree gchar *hsi6 = NULL;
	g_autofree gchar *hsi7 = NULL;
	g_autofree gchar *hsi8 = NULL;
	g_autofree gchar *hsi9 = NULL;
	g_autofree gchar *expected_hsi9 = NULL;
	g_autoptr(FuSecurityAttrs) attrs = NULL;
	g_autoptr(FwupdSecurityAttr) attr = NULL;

	/* no attrs */
	attrs = fu_security_attrs_new();
	hsi1 = fu_security_attrs_calculate_hsi(attrs, FU_SECURITY_ATTRS_FLAG_NONE);
	g_assert_cmpstr(hsi1, ==, "HSI:0");

	/* just success from HSI:1 */
	attr = fwupd_security_attr_new(FWUPD_SECURITY_ATTR_ID_SPI_BIOSWE);
	fwupd_security_attr_set_plugin(attr, "test");
	fwupd_security_attr_set_level(attr, FWUPD_SECURITY_ATTR_LEVEL_CRITICAL);
	fwupd_security_attr_add_flag(attr, FWUPD_SECURITY_ATTR_FLAG_SUCCESS);
	fwupd_security_attr_set_url(attr, "http://test");
	fu_security_attrs_append(attrs, attr);
	hsi2 = fu_security_attrs_calculate_hsi(attrs, FU_SECURITY_ATTRS_FLAG_NONE);
	g_assert_cmpstr(hsi2, ==, "HSI:1");
	g_clear_object(&attr);

	/* add failed from HSI:2, so still HSI:1 */
	attr = fwupd_security_attr_new("org.fwupd.hsi.PRX");
	fwupd_security_attr_set_plugin(attr, "test");
	fwupd_security_attr_set_level(attr, FWUPD_SECURITY_ATTR_LEVEL_IMPORTANT);
	fwupd_security_attr_set_url(attr, "http://test");
	fu_security_attrs_append(attrs, attr);
	hsi3 = fu_security_attrs_calculate_hsi(attrs, FU_SECURITY_ATTRS_FLAG_NONE);
	g_assert_cmpstr(hsi3, ==, "HSI:1");
	g_clear_object(&attr);

	/* add an implicit obsolete via duplication */
	attr = fwupd_security_attr_new("org.fwupd.hsi.PRX");
	fwupd_security_attr_set_plugin(attr, "other-plugin");
	fwupd_security_attr_set_level(attr, FWUPD_SECURITY_ATTR_LEVEL_IMPORTANT);
	fwupd_security_attr_set_url(attr, "http://other-plugin");
	fu_security_attrs_append(attrs, attr);
	fu_security_attrs_depsolve(attrs);
	hsi4 = fu_security_attrs_calculate_hsi(attrs, FU_SECURITY_ATTRS_FLAG_NONE);
	g_assert_cmpstr(hsi4, ==, "HSI:1");
	g_assert_true(fwupd_security_attr_has_flag(attr, FWUPD_SECURITY_ATTR_FLAG_OBSOLETED));
	g_clear_object(&attr);

	/* add attr from HSI:3, obsoleting the failure */
	attr = fwupd_security_attr_new("org.fwupd.hsi.BIOSGuard");
	fwupd_security_attr_set_plugin(attr, "test");
	fwupd_security_attr_set_level(attr, FWUPD_SECURITY_ATTR_LEVEL_THEORETICAL);
	fwupd_security_attr_add_flag(attr, FWUPD_SECURITY_ATTR_FLAG_SUCCESS);
	fwupd_security_attr_add_obsolete(attr, "org.fwupd.hsi.PRX");
	fwupd_security_attr_set_url(attr, "http://test");
	fu_security_attrs_append(attrs, attr);
	fu_security_attrs_depsolve(attrs);
	hsi5 = fu_security_attrs_calculate_hsi(attrs, FU_SECURITY_ATTRS_FLAG_NONE);
	g_assert_cmpstr(hsi5, ==, "HSI:3");
	g_clear_object(&attr);

	/* add taint that was fine */
	attr = fwupd_security_attr_new(FWUPD_SECURITY_ATTR_ID_FWUPD_PLUGINS);
	fwupd_security_attr_set_plugin(attr, "test");
	fwupd_security_attr_add_flag(attr, FWUPD_SECURITY_ATTR_FLAG_SUCCESS);
	fwupd_security_attr_add_flag(attr, FWUPD_SECURITY_ATTR_FLAG_RUNTIME_ISSUE);
	fwupd_security_attr_set_url(attr, "http://test");
	fu_security_attrs_append(attrs, attr);
	hsi6 = fu_security_attrs_calculate_hsi(attrs, FU_SECURITY_ATTRS_FLAG_NONE);
	g_assert_cmpstr(hsi6, ==, "HSI:3");
	g_clear_object(&attr);

	/* add updates and attestation */
	attr = fwupd_security_attr_new(FWUPD_SECURITY_ATTR_ID_FWUPD_UPDATES);
	fwupd_security_attr_set_plugin(attr, "test");
	fwupd_security_attr_add_flag(attr, FWUPD_SECURITY_ATTR_FLAG_SUCCESS);
	fwupd_security_attr_set_url(attr, "http://test");
	fu_security_attrs_append(attrs, attr);
	hsi7 = fu_security_attrs_calculate_hsi(attrs, FU_SECURITY_ATTRS_FLAG_NONE);
	g_assert_cmpstr(hsi7, ==, "HSI:3");
	g_clear_object(&attr);

	/* add issue that was uncool */
	attr = fwupd_security_attr_new(FWUPD_SECURITY_ATTR_ID_KERNEL_SWAP);
	fwupd_security_attr_set_plugin(attr, "test");
	fwupd_security_attr_add_flag(attr, FWUPD_SECURITY_ATTR_FLAG_RUNTIME_ISSUE);
	fwupd_security_attr_set_url(attr, "http://test");
	fu_security_attrs_append(attrs, attr);
	hsi8 = fu_security_attrs_calculate_hsi(attrs, FU_SECURITY_ATTRS_FLAG_NONE);
	g_assert_cmpstr(hsi8, ==, "HSI:3!");
	g_clear_object(&attr);

	/* show version in the attribute */
	attr = fwupd_security_attr_new(FWUPD_SECURITY_ATTR_ID_KERNEL_SWAP);
	fwupd_security_attr_set_plugin(attr, "test");
	fwupd_security_attr_add_flag(attr, FWUPD_SECURITY_ATTR_FLAG_RUNTIME_ISSUE);
	fwupd_security_attr_set_url(attr, "http://test");
	fu_security_attrs_append(attrs, attr);
	hsi9 = fu_security_attrs_calculate_hsi(attrs, FU_SECURITY_ATTRS_FLAG_ADD_VERSION);
	expected_hsi9 = g_strdup_printf("HSI:3! (v%d.%d.%d)",
					FWUPD_MAJOR_VERSION,
					FWUPD_MINOR_VERSION,
					FWUPD_MICRO_VERSION);
	g_assert_cmpstr(hsi9, ==, expected_hsi9);
	g_clear_object(&attr);
}
static void
fu_firmware_builder_round_trip_func(void)
{
	struct {
		GType gtype;
		const gchar *xml_fn;
		const gchar *checksum;
	} map[] = {
	    {FU_TYPE_DFUSE_FIRMWARE,
	     "dfuse.builder.xml",
	     "c1ff429f0e381c8fe8e1b2ee41a5a9a79e2f2ff7"},
	    {FU_TYPE_FDT_FIRMWARE, "fdt.builder.xml", "40f7fbaff684a6bcf67c81b3079422c2529741e1"},
	    {FU_TYPE_FIT_FIRMWARE, "fit.builder.xml", "293ce07351bb7d76631c4e2ba47243db1e150f3c"},
	    {FU_TYPE_SREC_FIRMWARE, "srec.builder.xml", "2aae6c35c94fcfb415dbe95f408b9ce91ee846ed"},
	    {FU_TYPE_IHEX_FIRMWARE, "ihex.builder.xml", "a8d74f767f3fc992b413e5ba801cedc80a4cf013"},
	    {FU_TYPE_FMAP_FIRMWARE, "fmap.builder.xml", "a0b9ffc10a586d217edf9e9bae7c1fe7c564ea01"},
	    {FU_TYPE_EFI_FIRMWARE_SECTION,
	     "efi-firmware-section.builder.xml",
	     "2aae6c35c94fcfb415dbe95f408b9ce91ee846ed"},
	    {FU_TYPE_EFI_FIRMWARE_SECTION,
	     "efi-firmware-section.builder.xml",
	     "2aae6c35c94fcfb415dbe95f408b9ce91ee846ed"},
	    {FU_TYPE_EFI_FIRMWARE_FILE,
	     "efi-firmware-file.builder.xml",
	     "1002c14b29a76069f3b7e35c50a55d2b0d197441"},
	    {FU_TYPE_EFI_FIRMWARE_FILESYSTEM,
	     "efi-firmware-filesystem.builder.xml",
	     "d6fbadc1c303a3b4eede9db7fb0ddb353efffc86"},
	    {FU_TYPE_EFI_FIRMWARE_VOLUME,
	     "efi-firmware-volume.builder.xml",
	     "2aae6c35c94fcfb415dbe95f408b9ce91ee846ed"},
	    {FU_TYPE_IFD_FIRMWARE, "ifd.builder.xml", "0805c742e0deec12db2d8f9a86158a7cf610869b"},
	    {FU_TYPE_CFU_OFFER,
	     "cfu-offer.builder.xml",
	     "acc572d03a129081921c36118b527dab34a077ad"},
	    {FU_TYPE_CFU_PAYLOAD,
	     "cfu-payload.builder.xml",
	     "5da829f5fd15a28970aed98ebb26ebf2f88ed6f2"},
	    {FU_TYPE_IFWI_CPD_FIRMWARE,
	     "ifwi-cpd.builder.xml",
	     "91e348d17cb91ef7a528e85beb39d15a0532dca5"},
	    {FU_TYPE_IFWI_FPT_FIRMWARE,
	     "ifwi-fpt.builder.xml",
	     "d1f0fb2c2a7a99441bf4a825d060642315a94d91"},
	    {FU_TYPE_OPROM_FIRMWARE,
	     "oprom.builder.xml",
	     "2aae6c35c94fcfb415dbe95f408b9ce91ee846ed"},
	    {FU_TYPE_INTEL_THUNDERBOLT_NVM,
	     "intel-thunderbolt.builder.xml",
	     "e858000646fecb5223b41df57647c005b495749b"},
#ifdef HAVE_CBOR
	    {FU_TYPE_USWID_FIRMWARE,
	     "uswid.builder.xml",
	     "cae8660d5acd5bb614d0410bc53dedaa1899aee1"},
#endif
	    {G_TYPE_INVALID, NULL, NULL}};
	g_type_ensure(FU_TYPE_COSWID_FIRMWARE);
	for (guint i = 0; map[i].gtype != G_TYPE_INVALID; i++) {
		gboolean ret;
		g_autofree gchar *csum1 = NULL;
		g_autofree gchar *csum2 = NULL;
		g_autofree gchar *filename = NULL;
		g_autofree gchar *xml1 = NULL;
		g_autofree gchar *xml2 = NULL;
		g_autoptr(FuFirmware) firmware1 = g_object_new(map[i].gtype, NULL);
		g_autoptr(FuFirmware) firmware2 = g_object_new(map[i].gtype, NULL);
		g_autoptr(FuFirmware) firmware3 = g_object_new(map[i].gtype, NULL);
		g_autoptr(GError) error = NULL;
		g_autoptr(GBytes) blob = NULL;

		/* build and write */
		filename = g_test_build_filename(G_TEST_DIST, "tests", map[i].xml_fn, NULL);
		ret = g_file_get_contents(filename, &xml1, NULL, &error);
		g_assert_no_error(error);
		g_assert_true(ret);
		ret = fu_firmware_build_from_xml(firmware1, xml1, &error);
		g_assert_no_error(error);
		g_assert_true(ret);
		csum1 = fu_firmware_get_checksum(firmware1, G_CHECKSUM_SHA1, &error);
		g_assert_no_error(error);
		g_assert_cmpstr(csum1, ==, map[i].checksum);

		/* ensure we can write and then parse what we just wrote */
		blob = fu_firmware_write(firmware1, &error);
		g_assert_no_error(error);
		g_assert_nonnull(blob);
		ret = fu_firmware_parse(firmware3, blob, FWUPD_INSTALL_FLAG_NO_SEARCH, &error);
		if (!ret)
			g_prefix_error(&error, "%s: ", map[i].xml_fn);
		g_assert_no_error(error);
		g_assert_true(ret);

		/* ensure we can round-trip */
		xml2 = fu_firmware_export_to_xml(firmware1, FU_FIRMWARE_EXPORT_FLAG_NONE, &error);
		g_assert_no_error(error);
		ret = fu_firmware_build_from_xml(firmware2, xml2, &error);
		g_assert_no_error(error);
		g_assert_true(ret);
		csum2 = fu_firmware_get_checksum(firmware2, G_CHECKSUM_SHA1, &error);
		g_assert_no_error(error);
		g_assert_cmpstr(csum2, ==, map[i].checksum);
	}
}

typedef struct {
	guint last_percentage;
	guint updates;
} FuProgressHelper;

static void
fu_progress_percentage_changed_cb(FuProgress *progress, guint percentage, gpointer data)
{
	FuProgressHelper *helper = (FuProgressHelper *)data;
	helper->last_percentage = percentage;
	helper->updates++;
}

static void
fu_progress_func(void)
{
	FuProgressHelper helper = {0};
	g_autoptr(FuProgress) progress = fu_progress_new(G_STRLOC);
	g_autofree gchar *str = NULL;

	g_signal_connect(FU_PROGRESS(progress),
			 "percentage-changed",
			 G_CALLBACK(fu_progress_percentage_changed_cb),
			 &helper);

	g_assert_cmpfloat_with_epsilon(fu_progress_get_duration(progress), 0.f, 0.001);

	fu_progress_set_profile(progress, TRUE);
	fu_progress_set_steps(progress, 5);
	g_assert_cmpint(helper.last_percentage, ==, 0);

	g_usleep(20 * 1000);
	fu_progress_step_done(progress);
	g_assert_cmpint(helper.updates, ==, 2);
	g_assert_cmpint(helper.last_percentage, ==, 20);

	for (guint i = 0; i < 4; i++) {
		g_usleep(20 * 1000);
		fu_progress_step_done(progress);
	}

	g_assert_cmpint(helper.last_percentage, ==, 100);
	g_assert_cmpint(helper.updates, ==, 6);
	g_assert_cmpfloat_with_epsilon(fu_progress_get_duration(progress), 0.1f, 0.05);
	str = fu_progress_traceback(progress);
	g_debug("\n%s", str);
}

static void
fu_progress_child_func(void)
{
	FuProgressHelper helper = {0};
	FuProgress *child;
	g_autoptr(FuProgress) progress = fu_progress_new(G_STRLOC);

	/* reset */
	fu_progress_set_profile(progress, TRUE);
	fu_progress_set_steps(progress, 2);
	g_signal_connect(FU_PROGRESS(progress),
			 "percentage-changed",
			 G_CALLBACK(fu_progress_percentage_changed_cb),
			 &helper);

	/* parent: |-----------------------|-----------------------|
	 * step1:  |-----------------------|
	 * child:                          |-------------|---------|
	 */

	/* PARENT UPDATE */
	g_debug("parent update #1");
	fu_progress_step_done(progress);
	g_assert_cmpint(helper.updates, ==, 1);
	g_assert_cmpint(helper.last_percentage, ==, 50);

	/* now test with a child */
	child = fu_progress_get_child(progress);
	fu_progress_set_id(child, G_STRLOC);
	fu_progress_set_steps(child, 2);

	g_debug("child update #1");
	fu_progress_step_done(child);
	g_assert_cmpint(helper.updates, ==, 2);
	g_assert_cmpint(helper.last_percentage, ==, 75);

	/* child update */
	g_debug("child update #2");
	fu_progress_step_done(child);
	g_assert_cmpint(helper.updates, ==, 3);
	g_assert_cmpint(helper.last_percentage, ==, 100);

	/* parent update */
	g_debug("parent update #2");
	fu_progress_step_done(progress);

	/* ensure we ignored the duplicate */
	g_assert_cmpint(helper.updates, ==, 3);
	g_assert_cmpint(helper.last_percentage, ==, 100);
}

static void
fu_progress_parent_one_step_proxy_func(void)
{
	FuProgressHelper helper = {0};
	FuProgress *child;
	g_autoptr(FuProgress) progress = fu_progress_new(G_STRLOC);

	/* one step */
	fu_progress_set_steps(progress, 1);
	g_signal_connect(FU_PROGRESS(progress),
			 "percentage-changed",
			 G_CALLBACK(fu_progress_percentage_changed_cb),
			 &helper);

	/* now test with a child */
	child = fu_progress_get_child(progress);
	fu_progress_set_id(child, G_STRLOC);
	fu_progress_set_steps(child, 2);

	/* child set value */
	fu_progress_set_percentage(child, 33);

	/* ensure 1 updates for progress with one step and ensure using child value as parent */
	g_assert_cmpint(helper.updates, ==, 1);
	g_assert_cmpint(helper.last_percentage, ==, 33);
}

static void
fu_progress_non_equal_steps_func(void)
{
	g_autoptr(FuProgress) progress = fu_progress_new(G_STRLOC);
	FuProgress *child;
	FuProgress *grandchild;

	/* test non-equal steps */
	fu_progress_set_id(progress, G_STRLOC);
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_ERASE, 20, NULL);
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_WRITE, 60, NULL);
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_READ, 20, NULL);
	g_assert_cmpint(fu_progress_get_percentage(progress), ==, 0);
	g_assert_cmpint(fu_progress_get_status(progress), ==, FWUPD_STATUS_DEVICE_ERASE);

	/* child step should increment according to the custom steps */
	child = fu_progress_get_child(progress);
	fu_progress_set_id(child, G_STRLOC);
	fu_progress_set_steps(child, 2);
	fu_progress_set_status(child, FWUPD_STATUS_DEVICE_BUSY);
	g_assert_cmpint(fu_progress_get_status(progress), ==, FWUPD_STATUS_DEVICE_BUSY);

	/* start child */
	fu_progress_step_done(child);

	/* verify 10% */
	g_assert_cmpint(fu_progress_get_percentage(progress), ==, 10);

	/* finish child */
	fu_progress_step_done(child);

	/* ensure the parent is switched back to the status before the child took over */
	g_assert_cmpint(fu_progress_get_status(progress), ==, FWUPD_STATUS_DEVICE_ERASE);

	fu_progress_step_done(progress);
	g_assert_cmpint(fu_progress_get_status(progress), ==, FWUPD_STATUS_DEVICE_WRITE);

	/* verify 20% */
	g_assert_cmpint(fu_progress_get_percentage(progress), ==, 20);

	/* child step should increment according to the custom steps */
	child = fu_progress_get_child(progress);
	fu_progress_set_id(child, G_STRLOC);
	fu_progress_set_id(child, G_STRLOC);
	fu_progress_add_step(child, FWUPD_STATUS_DEVICE_RESTART, 25, NULL);
	fu_progress_add_step(child, FWUPD_STATUS_DEVICE_WRITE, 75, NULL);
	g_assert_cmpint(fu_progress_get_status(progress), ==, FWUPD_STATUS_DEVICE_RESTART);

	/* start child */
	fu_progress_step_done(child);
	g_assert_cmpint(fu_progress_get_status(progress), ==, FWUPD_STATUS_DEVICE_WRITE);

	/* verify bilinear interpolation is working */
	g_assert_cmpint(fu_progress_get_percentage(progress), ==, 35);

	/*
	 * 0        20                             80         100
	 * |---------||----------------------------||---------|
	 *            |       35                   |
	 *            |-------||-------------------| (25%)
	 *                     |              75.5 |
	 *                     |---------------||--| (90%)
	 */
	grandchild = fu_progress_get_child(child);
	fu_progress_set_id(grandchild, G_STRLOC);
	fu_progress_add_step(grandchild, FWUPD_STATUS_DEVICE_ERASE, 90, NULL);
	fu_progress_add_step(grandchild, FWUPD_STATUS_DEVICE_WRITE, 10, NULL);

	fu_progress_step_done(grandchild);

	/* verify bilinear interpolation (twice) is working for subpercentage */
	g_assert_cmpint(fu_progress_get_percentage(progress), ==, 75);

	fu_progress_step_done(grandchild);

	/* finish child */
	fu_progress_step_done(child);

	fu_progress_step_done(progress);
	g_assert_cmpint(fu_progress_get_status(progress), ==, FWUPD_STATUS_DEVICE_READ);

	/* verify 80% */
	g_assert_cmpint(fu_progress_get_percentage(progress), ==, 80);

	fu_progress_step_done(progress);

	/* verify 100% */
	g_assert_cmpint(fu_progress_get_percentage(progress), ==, 100);
	g_assert_cmpint(fu_progress_get_status(progress), ==, FWUPD_STATUS_UNKNOWN);
}

static void
fu_progress_finish_func(void)
{
	FuProgress *child;
	g_autoptr(FuProgress) progress = fu_progress_new(G_STRLOC);

	/* check straight finish */
	fu_progress_set_steps(progress, 3);

	child = fu_progress_get_child(progress);
	fu_progress_set_id(child, G_STRLOC);
	fu_progress_set_steps(child, 3);
	fu_progress_finished(child);

	/* parent step done after child finish */
	fu_progress_step_done(progress);
}

static void
fu_progress_child_finished(void)
{
	FuProgress *child;
	g_autoptr(FuProgress) progress = fu_progress_new(G_STRLOC);

	/* check straight finish */
	fu_progress_set_steps(progress, 3);

	child = fu_progress_get_child(progress);
	fu_progress_set_id(child, G_STRLOC);
	fu_progress_set_steps(child, 3);
	/* some imaginary igorable error */

	/* parent step done after child finish */
	fu_progress_add_flag(progress, FU_PROGRESS_FLAG_CHILD_FINISHED);
	fu_progress_step_done(progress);
}

int
main(int argc, char **argv)
{
	g_autofree gchar *testdatadir = NULL;

	g_test_init(&argc, &argv, NULL);
	g_type_ensure(FU_TYPE_IFD_BIOS);

	/* only critical and error are fatal */
	g_log_set_fatal_mask(NULL, G_LOG_LEVEL_ERROR | G_LOG_LEVEL_CRITICAL);
	(void)g_setenv("G_MESSAGES_DEBUG", "all", TRUE);

	testdatadir = g_test_build_filename(G_TEST_DIST, "tests", NULL);
	(void)g_setenv("FWUPD_DATADIR", testdatadir, TRUE);
	(void)g_setenv("FWUPD_PLUGINDIR", testdatadir, TRUE);
	(void)g_setenv("FWUPD_SYSCONFDIR", testdatadir, TRUE);
	(void)g_setenv("FWUPD_SYSFSFWATTRIBDIR", testdatadir, TRUE);
	(void)g_setenv("FWUPD_OFFLINE_TRIGGER", "/tmp/fwupd-self-test/system-update", TRUE);
	(void)g_setenv("FWUPD_LOCALSTATEDIR", "/tmp/fwupd-self-test/var", TRUE);
	(void)g_setenv("FWUPD_PROFILE", "1", TRUE);

	g_test_add_func("/fwupd/common{strnsplit}", fu_strsplit_func);
	g_test_add_func("/fwupd/common{memmem}", fu_common_memmem_func);
	g_test_add_func("/fwupd/progress", fu_progress_func);
	g_test_add_func("/fwupd/progress{child}", fu_progress_child_func);
	g_test_add_func("/fwupd/progress{child-finished}", fu_progress_child_finished);
	g_test_add_func("/fwupd/progress{parent-1-step}", fu_progress_parent_one_step_proxy_func);
	g_test_add_func("/fwupd/progress{no-equal}", fu_progress_non_equal_steps_func);
	g_test_add_func("/fwupd/progress{finish}", fu_progress_finish_func);
	g_test_add_func("/fwupd/bios-attrs{load}", fu_bios_settings_load_func);
	g_test_add_func("/fwupd/security-attrs{hsi}", fu_security_attrs_hsi_func);
	g_test_add_func("/fwupd/plugin{config}", fu_plugin_config_func);
	g_test_add_func("/fwupd/plugin{devices}", fu_plugin_devices_func);
	g_test_add_func("/fwupd/plugin{device-inhibit-children}",
			fu_plugin_device_inhibit_children_func);
	g_test_add_func("/fwupd/plugin{delay}", fu_plugin_delay_func);
	g_test_add_func("/fwupd/plugin{quirks}", fu_plugin_quirks_func);
	g_test_add_func("/fwupd/plugin{quirks-performance}", fu_plugin_quirks_performance_func);
	g_test_add_func("/fwupd/plugin{quirks-device}", fu_plugin_quirks_device_func);
	g_test_add_func("/fwupd/backend", fu_backend_func);
	g_test_add_func("/fwupd/chunk", fu_chunk_func);
	g_test_add_func("/fwupd/common{align-up}", fu_common_align_up_func);
	g_test_add_func("/fwupd/common{gpt-type}", fu_common_gpt_type_func);
	g_test_add_func("/fwupd/common{byte-array}", fu_common_byte_array_func);
	g_test_add_func("/fwupd/common{crc}", fu_common_crc_func);
	g_test_add_func("/fwupd/common{string-append-kv}", fu_string_append_func);
	g_test_add_func("/fwupd/common{version-guess-format}", fu_version_guess_format_func);
	g_test_add_func("/fwupd/common{strtoull}", fu_strtoull_func);
	g_test_add_func("/fwupd/common{version}", fu_common_version_func);
	g_test_add_func("/fwupd/common{version-semver}", fu_version_semver_func);
	g_test_add_func("/fwupd/common{vercmp}", fu_common_vercmp_func);
	g_test_add_func("/fwupd/common{strstrip}", fu_strstrip_func);
	g_test_add_func("/fwupd/common{endian}", fu_common_endian_func);
	g_test_add_func("/fwupd/common{cabinet}", fu_common_cabinet_func);
	g_test_add_func("/fwupd/common{bytes-get-data}", fu_common_bytes_get_data_func);
	g_test_add_func("/fwupd/common{kernel-lockdown}", fu_common_kernel_lockdown_func);
	g_test_add_func("/fwupd/common{strsafe}", fu_strsafe_func);
	g_test_add_func("/fwupd/efivar", fu_efivar_func);
	g_test_add_func("/fwupd/hwids", fu_hwids_func);
	g_test_add_func("/fwupd/smbios", fu_smbios_func);
	g_test_add_func("/fwupd/smbios3", fu_smbios3_func);
	g_test_add_func("/fwupd/smbios{dt}", fu_smbios_dt_func);
	g_test_add_func("/fwupd/smbios{dt-fallback}", fu_smbios_dt_fallback_func);
	g_test_add_func("/fwupd/smbios{class}", fu_smbios_class_func);
	g_test_add_func("/fwupd/firmware", fu_firmware_func);
	g_test_add_func("/fwupd/firmware{common}", fu_firmware_common_func);
	g_test_add_func("/fwupd/firmware{linear}", fu_firmware_linear_func);
	g_test_add_func("/fwupd/firmware{dedupe}", fu_firmware_dedupe_func);
	g_test_add_func("/fwupd/firmware{build}", fu_firmware_build_func);
	g_test_add_func("/fwupd/firmware{raw-aligned}", fu_firmware_raw_aligned_func);
	g_test_add_func("/fwupd/firmware{ihex}", fu_firmware_ihex_func);
	g_test_add_func("/fwupd/firmware{ihex-offset}", fu_firmware_ihex_offset_func);
	g_test_add_func("/fwupd/firmware{ihex-signed}", fu_firmware_ihex_signed_func);
	g_test_add_func("/fwupd/firmware{srec-tokenization}", fu_firmware_srec_tokenization_func);
	g_test_add_func("/fwupd/firmware{srec}", fu_firmware_srec_func);
	g_test_add_func("/fwupd/firmware{fdt}", fu_firmware_fdt_func);
	g_test_add_func("/fwupd/firmware{fit}", fu_firmware_fit_func);
	g_test_add_func("/fwupd/firmware{ifwi-cpd}", fu_firmware_ifwi_cpd_func);
	g_test_add_func("/fwupd/firmware{ifwi-fpt}", fu_firmware_ifwi_fpt_func);
	g_test_add_func("/fwupd/firmware{oprom}", fu_firmware_oprom_func);
	g_test_add_func("/fwupd/firmware{dfu}", fu_firmware_dfu_func);
	g_test_add_func("/fwupd/firmware{dfu-patch}", fu_firmware_dfu_patch_func);
	g_test_add_func("/fwupd/firmware{dfuse}", fu_firmware_dfuse_func);
	g_test_add_func("/fwupd/firmware{builder-round-trip}", fu_firmware_builder_round_trip_func);
	g_test_add_func("/fwupd/firmware{fmap}", fu_firmware_fmap_func);
	g_test_add_func("/fwupd/firmware{gtypes}", fu_firmware_new_from_gtypes_func);
	g_test_add_func("/fwupd/archive{invalid}", fu_archive_invalid_func);
	g_test_add_func("/fwupd/archive{cab}", fu_archive_cab_func);
	g_test_add_func("/fwupd/device", fu_device_func);
	g_test_add_func("/fwupd/device{instance-ids}", fu_device_instance_ids_func);
	g_test_add_func("/fwupd/device{composite-id}", fu_device_composite_id_func);
	g_test_add_func("/fwupd/device{flags}", fu_device_flags_func);
	g_test_add_func("/fwupd/device{custom-flags}", fu_device_private_flags_func);
	g_test_add_func("/fwupd/device{inhibit}", fu_device_inhibit_func);
	g_test_add_func("/fwupd/device{inhibit-updateable}", fu_device_inhibit_updateable_func);
	g_test_add_func("/fwupd/device{parent}", fu_device_parent_func);
	g_test_add_func("/fwupd/device{children}", fu_device_children_func);
	g_test_add_func("/fwupd/device{incorporate}", fu_device_incorporate_func);
	if (g_test_slow())
		g_test_add_func("/fwupd/device{poll}", fu_device_poll_func);
	g_test_add_func("/fwupd/device-locker{success}", fu_device_locker_func);
	g_test_add_func("/fwupd/device-locker{fail}", fu_device_locker_fail_func);
	g_test_add_func("/fwupd/device{name}", fu_device_name_func);
	g_test_add_func("/fwupd/device{metadata}", fu_device_metadata_func);
	g_test_add_func("/fwupd/device{open-refcount}", fu_device_open_refcount_func);
	g_test_add_func("/fwupd/device{version-format}", fu_device_version_format_func);
	g_test_add_func("/fwupd/device{retry-success}", fu_device_retry_success_func);
	g_test_add_func("/fwupd/device{retry-failed}", fu_device_retry_failed_func);
	g_test_add_func("/fwupd/device{retry-hardware}", fu_device_retry_hardware_func);
	g_test_add_func("/fwupd/device{cfi-device}", fu_device_cfi_device_func);
	return g_test_run();
}
