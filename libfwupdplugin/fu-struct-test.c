/*
 * Copyright 2017 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include <fwupdplugin.h>

#include "fu-self-test-struct.h"
#include "fu-test.h"

static void
fu_plugin_struct_bits_func(void)
{
	g_autofree gchar *str1 = NULL;
	g_autoptr(FuStructSelfTestBits) st2 = NULL;
	g_autoptr(FuStructSelfTestBits) st = fu_struct_self_test_bits_new();
	g_autoptr(GError) error = NULL;

	/* 0b1111 + 0b1 + 0b0010 = 0b111110010 -> 0x1F2 */
	g_assert_cmpint(st->buf->len, ==, 4);
	fu_dump_raw(G_LOG_DOMAIN, "buf", st->buf->data, st->buf->len);
	g_assert_cmpint(st->buf->data[0], ==, 0xF2);
	g_assert_cmpint(st->buf->data[1], ==, 0x01);
	g_assert_cmpint(st->buf->data[2], ==, 0x0);
	g_assert_cmpint(st->buf->data[3], ==, 0x0);

	st2 = fu_struct_self_test_bits_parse(st->buf->data, st->buf->len, 0x0, &error);
	g_assert_no_error(error);
	g_assert_nonnull(st2);

	g_assert_cmpint(fu_struct_self_test_bits_get_lower(st2), ==, 0x2);
	g_assert_cmpint(fu_struct_self_test_bits_get_middle(st2), ==, 0b1);
	g_assert_cmpint(fu_struct_self_test_bits_get_upper(st2), ==, 0xF);

	str1 = fu_struct_self_test_bits_to_string(st2);
	g_assert_cmpstr(str1,
			==,
			"FuStructSelfTestBits:\n"
			"  lower: 0x2 [two]\n"
			"  middle: 0x1\n"
			"  upper: 0xf");

	/* set all to maximum value */
	fu_struct_self_test_bits_set_lower(st2, G_MAXUINT32);
	fu_struct_self_test_bits_set_middle(st2, G_MAXUINT32);
	fu_struct_self_test_bits_set_upper(st2, G_MAXUINT32);
	g_assert_cmpint(fu_struct_self_test_bits_get_lower(st2), ==, 0xF);
	g_assert_cmpint(fu_struct_self_test_bits_get_middle(st2), ==, 0x1);
	g_assert_cmpint(fu_struct_self_test_bits_get_upper(st2), ==, 0xF);
}

static void
fu_plugin_struct_list_func(void)
{
	g_autofree gchar *str = NULL;
	g_autoptr(FuStructSelfTestList) st = fu_struct_self_test_list_new();

	for (guint i = 0; i < FU_STRUCT_SELF_TEST_LIST_N_ELEMENTS_BASIC; i++) {
		fu_struct_self_test_list_set_basic(st, i, i * 16);
		g_assert_cmpint(fu_struct_self_test_list_get_basic(st, i), ==, i * 16);
	}

	for (guint i = 0; i < FU_STRUCT_SELF_TEST_LIST_N_ELEMENTS_MEMBERS; i++) {
		gboolean ret;
		g_autoptr(FuStructSelfTestListMember) st2 = fu_struct_self_test_list_member_new();
		g_autoptr(FuStructSelfTestListMember) st3 = NULL;
		g_autoptr(GError) error = NULL;

		fu_struct_self_test_list_member_set_data1(st2, i * 16);
		fu_struct_self_test_list_member_set_data2(st2, i * 32);
		ret = fu_struct_self_test_list_set_members(st, i, st2, &error);
		g_assert_no_error(error);
		g_assert_true(ret);

		st3 = fu_struct_self_test_list_get_members(st, i);
		g_assert_cmpint(fu_struct_self_test_list_member_get_data1(st3), ==, i * 16);
		g_assert_cmpint(fu_struct_self_test_list_member_get_data2(st3), ==, i * 32);
	}

	/* size */
	str = fu_byte_array_to_string(st->buf);
	g_assert_cmpstr(
	    str,
	    ==,
	    "000000001000000020000000300000004000000050000000600000007000000000001020204030604080");
}

static void
fu_plugin_struct_func(void)
{
	gboolean ret;
	g_autoptr(FuStructSelfTest) st = fu_struct_self_test_new();
	g_autoptr(FuStructSelfTest) st2 = NULL;
	g_autoptr(FuStructSelfTest) st3 = NULL;
	g_autoptr(GError) error = NULL;
	g_autofree gchar *str1 = NULL;
	g_autofree gchar *str2 = NULL;
	g_autofree gchar *oem_table_id = NULL;

	/* size */
	g_assert_cmpint(st->buf->len, ==, 59);

	/* getters and setters */
	fu_struct_self_test_set_revision(st, 0xFF);
	fu_struct_self_test_set_length(st, 0xDEAD);
	ret = fu_struct_self_test_set_oem_table_id(st, "X", &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	g_assert_cmpint(fu_struct_self_test_get_revision(st), ==, 0xFF);
	g_assert_cmpint(fu_struct_self_test_get_length(st), ==, 0xDEAD);

	/* pack */
	str1 = fu_byte_array_to_string(st->buf);
	g_assert_cmpstr(str1,
			==,
			"12345678adde0000ff000000000000000000000000000000004142434445465800000000"
			"00000000000000dfdfdfdf00000000ffffffffffffffff");

	/* parse */
	st2 = fu_struct_self_test_parse(st->buf->data, st->buf->len, 0x0, &error);
	g_assert_no_error(error);
	g_assert_nonnull(st2);
	g_assert_cmpint(fu_struct_self_test_get_revision(st2), ==, 0xFF);
	g_assert_cmpint(fu_struct_self_test_get_length(st2), ==, 0xDEAD);
	oem_table_id = fu_struct_self_test_get_oem_table_id(st2);
	g_assert_cmpstr(oem_table_id, ==, "X");

	/* to string */
	str2 = fu_struct_self_test_to_string(st);
	ret = fu_test_compare_lines(str2,
				    "FuStructSelfTest:\n"
				    "  signature: 0x12345678\n"
				    "  length: 0xdead\n"
				    "  revision: 0xff [all]\n"
				    "  owner: 00000000-0000-0000-0000-000000000000\n"
				    "  oem_id: ABCDEF\n"
				    "  oem_table_id: X\n"
				    "  oem_revision: 0x0\n"
				    "  asl_compiler_id: 0xDFDFDFDF\n"
				    "  asl_compiler_revision: 0x0",
				    &error);
	g_assert_no_error(error);
	g_assert_true(ret);

	/* parse failing signature */
	st->buf->data[0] = 0xFF;
	st3 = fu_struct_self_test_parse(st->buf->data, st->buf->len, 0x0, &error);
	g_assert_error(error, FWUPD_ERROR, FWUPD_ERROR_INVALID_DATA);
	g_assert_null(st3);
	g_clear_error(&error);
	ret = fu_struct_self_test_validate(st->buf->data, st->buf->len, 0x0, &error);
	g_assert_error(error, FWUPD_ERROR, FWUPD_ERROR_INVALID_DATA);
	g_assert_false(ret);
}

static void
fu_plugin_struct_wrapped_func(void)
{
	gboolean ret;
	g_autofree gchar *str1 = NULL;
	g_autofree gchar *str2 = NULL;
	g_autofree gchar *str4 = NULL;
	g_autoptr(FuStructSelfTestWrapped) st2 = NULL;
	g_autoptr(FuStructSelfTestWrapped) st3 = NULL;
	g_autoptr(FuStructSelfTest) st_base2 = NULL;
	g_autoptr(FuStructSelfTest) st_base = fu_struct_self_test_new();
	g_autoptr(FuStructSelfTestWrapped) st = fu_struct_self_test_wrapped_new();
	g_autoptr(GError) error = NULL;

	/* size */
	g_assert_cmpint(st->buf->len, ==, 61);

	/* getters and setters */
	fu_struct_self_test_wrapped_set_less(st, 0x99);
	fu_struct_self_test_wrapped_set_more(st, 0x12);
	g_assert_cmpint(fu_struct_self_test_wrapped_get_more(st), ==, 0x12);
	str1 = fu_byte_array_to_string(st->buf);
	g_assert_cmpstr(str1,
			==,
			"99123456783b000000000000000000000000000000000000000041424344454600000000"
			"0000000000000000dfdfdfdf00000000ffffffffffffffff12");

	/* modify the base */
	fu_struct_self_test_set_revision(st_base, 0xFE);
	ret = fu_struct_self_test_wrapped_set_base(st, st_base, &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	str4 = fu_byte_array_to_string(st->buf);
	g_assert_cmpstr(str4,
			==,
			"99123456783b000000fe0000000000000000000000000000000041424344454600000000"
			"0000000000000000dfdfdfdf00000000ffffffffffffffff12");

	/* parse */
	st2 = fu_struct_self_test_wrapped_parse(st->buf->data, st->buf->len, 0x0, &error);
	g_assert_no_error(error);
	g_assert_nonnull(st2);
	g_assert_cmpint(fu_struct_self_test_wrapped_get_more(st), ==, 0x12);
	st_base2 = fu_struct_self_test_wrapped_get_base(st);
	g_assert_cmpint(fu_struct_self_test_get_revision(st_base2), ==, 0xFE);

	/* to string */
	str2 = fu_struct_self_test_wrapped_to_string(st);
	g_debug("%s", str2);
	fu_test_compare_lines(str2,
			      "FuStructSelfTestWrapped:\n"
			      "  less: 0x99\n"
			      "  base: FuStructSelfTest:\n"
			      "  signature: 0x12345678\n"
			      "  length: 0x3b\n"
			      "  revision: 0xfe\n"
			      "  owner: 00000000-0000-0000-0000-000000000000\n"
			      "  oem_id: ABCDEF\n"
			      "  oem_revision: 0x0\n"
			      "  asl_compiler_id: 0xDFDFDFDF\n"
			      "  asl_compiler_revision: 0x0\n"
			      "  more: 0x12",
			      &error);
	g_assert_no_error(error);
	g_assert_true(ret);

	/* parse failing signature */
	st->buf->data[FU_STRUCT_SELF_TEST_WRAPPED_OFFSET_BASE] = 0xFF;
	st3 = fu_struct_self_test_wrapped_parse(st->buf->data, st->buf->len, 0x0, &error);
	g_assert_error(error, FWUPD_ERROR, FWUPD_ERROR_INVALID_DATA);
	g_assert_null(st3);
	g_clear_error(&error);
	ret = fu_struct_self_test_wrapped_validate(st->buf->data, st->buf->len, 0x0, &error);
	g_assert_error(error, FWUPD_ERROR, FWUPD_ERROR_INVALID_DATA);
	g_assert_false(ret);
}

int
main(int argc, char **argv)
{
	g_test_init(&argc, &argv, NULL);
	g_test_add_func("/fwupd/struct", fu_plugin_struct_func);
	g_test_add_func("/fwupd/struct/bits", fu_plugin_struct_bits_func);
	g_test_add_func("/fwupd/struct/list", fu_plugin_struct_list_func);
	g_test_add_func("/fwupd/struct/wrapped", fu_plugin_struct_wrapped_func);
	return g_test_run();
}
