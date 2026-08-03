/*
 * Copyright 2026 FocalTech Systems Co., Ltd.
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include "fu-focal-moc-common.h"

static void
fu_focal_moc_packet_build_func(void)
{
	const guint8 payload[] = {0x01};
	const guint8 expected[] = {0x02, 0x00, 0x02, 0x32, 0x01, 0x31};
	g_autoptr(GByteArray) pkt = NULL;
	g_autoptr(GError) error = NULL;

	pkt = fu_focal_moc_packet_new(0x32, payload, sizeof(payload), &error);
	g_assert_no_error(error);
	g_assert_nonnull(pkt);
	g_assert_cmpmem(pkt->data, pkt->len, expected, sizeof(expected));
}

static void
fu_focal_moc_packet_build_empty_func(void)
{
	const guint8 expected[] = {0x02, 0x00, 0x01, 0x30, 0x31};
	g_autoptr(GByteArray) pkt = NULL;
	g_autoptr(GError) error = NULL;

	pkt = fu_focal_moc_packet_new(0x30, NULL, 0, &error);
	g_assert_no_error(error);
	g_assert_nonnull(pkt);
	g_assert_cmpmem(pkt->data, pkt->len, expected, sizeof(expected));
}

static void
fu_focal_moc_packet_parse_func(void)
{
	const guint8 buf[] = {0x02, 0x00, 0x03, 0x04, 0xAA, 0xBB, 0x16};
	const guint8 expected[] = {0xAA, 0xBB};
	g_autoptr(GByteArray) data = NULL;
	g_autoptr(GError) error = NULL;

	data = fu_focal_moc_packet_parse(buf, sizeof(buf), &error);
	g_assert_no_error(error);
	g_assert_nonnull(data);
	g_assert_cmpmem(data->data, data->len, expected, sizeof(expected));
}

static void
fu_focal_moc_packet_parse_padded_func(void)
{
	/* trailing padding must not be treated as payload */
	const guint8 buf[] = {0x02, 0x00, 0x03, 0x04, 0xAA, 0xBB, 0x16, 0x00, 0x00, 0x00};
	const guint8 expected[] = {0xAA, 0xBB};
	g_autoptr(GByteArray) data = NULL;
	g_autoptr(GError) error = NULL;

	data = fu_focal_moc_packet_parse(buf, sizeof(buf), &error);
	g_assert_no_error(error);
	g_assert_nonnull(data);
	g_assert_cmpmem(data->data, data->len, expected, sizeof(expected));
}

static void
fu_focal_moc_packet_parse_bad_bcc_func(void)
{
	const guint8 buf[] = {0x02, 0x00, 0x03, 0x04, 0xAA, 0xBB, 0x17};
	g_autoptr(GByteArray) data = NULL;
	g_autoptr(GError) error = NULL;

	data = fu_focal_moc_packet_parse(buf, sizeof(buf), &error);
	g_assert_error(error, FWUPD_ERROR, FWUPD_ERROR_INVALID_DATA);
	g_assert_null(data);
}

static void
fu_focal_moc_packet_parse_truncated_func(void)
{
	const guint8 buf[] = {0x02, 0x00, 0x09, 0x04, 0xAA};
	g_autoptr(GByteArray) data = NULL;
	g_autoptr(GError) error = NULL;

	data = fu_focal_moc_packet_parse(buf, sizeof(buf), &error);
	g_assert_error(error, FWUPD_ERROR, FWUPD_ERROR_INVALID_DATA);
	g_assert_null(data);
}

static void
fu_focal_moc_packet_parse_nak_func(void)
{
	const guint8 buf[] = {0x02, 0x00, 0x01, 0x09, 0x08};
	g_autoptr(GByteArray) data = NULL;
	g_autoptr(GError) error = NULL;

	data = fu_focal_moc_packet_parse(buf, sizeof(buf), &error);
	g_assert_nonnull(error);
	g_assert_null(data);
}

static void
fu_focal_moc_packet_parse_zero_len_func(void)
{
	const guint8 buf[] = {0x02, 0x00, 0x00, 0x04, 0x04};
	g_autoptr(GByteArray) data = NULL;
	g_autoptr(GError) error = NULL;

	data = fu_focal_moc_packet_parse(buf, sizeof(buf), &error);
	g_assert_error(error, FWUPD_ERROR, FWUPD_ERROR_INVALID_DATA);
	g_assert_null(data);
}

static void
fu_focal_moc_version_func(void)
{
	const struct {
		const gchar *payload;
		const gchar *version;
		gboolean is_bootloader;
	} tests[] = {
	    {"FT9769DL_APP_FT9001_USB_DEC_SV0.1_7396", "7396", FALSE},
	    {"FT9365_APP_FT9001_A27A_USB_DEC_SV0.1_7004", "7004", FALSE},
	    {"FT9349_IAP_FT9001_A97A_USB_DEC_SV0.1_6704", "6704", TRUE},
	    {"FT9349_APP_FT9001_A97A_USB_DEC_SV0.1_f132", "f132", FALSE},
	};

	for (guint i = 0; i < G_N_ELEMENTS(tests); i++) {
		gboolean is_bootloader = FALSE;
		g_autofree gchar *version = NULL;
		g_autoptr(GError) error = NULL;

		version = fu_focal_moc_version_parse((const guint8 *)tests[i].payload,
						     strlen(tests[i].payload),
						     &is_bootloader,
						     &error);
		g_assert_no_error(error);
		g_assert_cmpstr(version, ==, tests[i].version);
		g_assert_cmpint(is_bootloader, ==, tests[i].is_bootloader);
	}
}

static void
fu_focal_moc_version_frame_func(void)
{
	/* the firmware writes the string by length, so the checksum sits hard
	 * against the last character and only the padding after it would ever
	 * act as a terminator */
	const gchar *str = "FT9365_APP_FT9001_A27A_USB_DEC_SV0.1_7004";
	gsize strsz = strlen(str);
	gboolean is_bootloader = TRUE;
	guint8 buf[64] = {0x02, 0x00, 0x00, 0x04};
	g_autofree gchar *version = NULL;
	g_autoptr(GByteArray) data = NULL;
	g_autoptr(GError) error = NULL;

	fu_memwrite_uint16(buf + 0x1, strsz + 1, G_BIG_ENDIAN);
	g_assert_true(
	    fu_memcpy_safe(buf, sizeof(buf), 0x4, (const guint8 *)str, strsz, 0x0, strsz, &error));
	buf[0x4 + strsz] = fu_xor8(buf + 0x1, 0x3 + strsz);

	/* matches the checksum captured from real hardware */
	g_assert_cmpuint(buf[0x4 + strsz], ==, 0x1b);

	data = fu_focal_moc_packet_parse(buf, sizeof(buf), &error);
	g_assert_no_error(error);
	g_assert_nonnull(data);
	version = fu_focal_moc_version_parse(data->data, data->len, &is_bootloader, &error);
	g_assert_no_error(error);
	g_assert_cmpstr(version, ==, "7004");
	g_assert_false(is_bootloader);
}

static void
fu_focal_moc_version_invalid_func(void)
{
	const gchar *tests[] = {
	    "",						/* empty */
	    "FT9349_FT9001_A97A_USB_DEC_SV0.1_6704",	/* no mode marker */
	    "FT9349_APP_IAP_FT9001_USB_DEC_SV0.1_6704", /* both mode markers */
	    "FT9349_APP_FT9001_A97A_USB_DEC_SV0.1_",	/* no build number */
	    "FT9349_APP_FT9001_\xff\xff_SV0.1_6704",	/* not utf-8 */
	};

	for (guint i = 0; i < G_N_ELEMENTS(tests); i++) {
		gboolean is_bootloader = FALSE;
		g_autofree gchar *version = NULL;
		g_autoptr(GError) error = NULL;

		version = fu_focal_moc_version_parse((const guint8 *)tests[i],
						     strlen(tests[i]),
						     &is_bootloader,
						     &error);
		g_assert_error(error, FWUPD_ERROR, FWUPD_ERROR_INVALID_DATA);
		g_assert_null(version);
	}
}

int
main(int argc, char **argv)
{
	g_test_init(&argc, &argv, NULL);
	g_test_add_func("/focal-moc/packet/build", fu_focal_moc_packet_build_func);
	g_test_add_func("/focal-moc/packet/build-empty", fu_focal_moc_packet_build_empty_func);
	g_test_add_func("/focal-moc/packet/parse", fu_focal_moc_packet_parse_func);
	g_test_add_func("/focal-moc/packet/parse-padded", fu_focal_moc_packet_parse_padded_func);
	g_test_add_func("/focal-moc/packet/bad-bcc", fu_focal_moc_packet_parse_bad_bcc_func);
	g_test_add_func("/focal-moc/packet/truncated", fu_focal_moc_packet_parse_truncated_func);
	g_test_add_func("/focal-moc/packet/nak", fu_focal_moc_packet_parse_nak_func);
	g_test_add_func("/focal-moc/packet/zero-len", fu_focal_moc_packet_parse_zero_len_func);
	g_test_add_func("/focal-moc/version", fu_focal_moc_version_func);
	g_test_add_func("/focal-moc/version/frame", fu_focal_moc_version_frame_func);
	g_test_add_func("/focal-moc/version/invalid", fu_focal_moc_version_invalid_func);
	return g_test_run();
}
