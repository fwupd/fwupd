/*
 * Copyright 2026 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include <fwupdplugin.h>

/* the on-the-wire size of a CFU offer, see the CFU specification */
#define FU_CFU_OFFER_TEST_SIZE 16

static void
fu_cfu_offer_default_func(void)
{
	g_autoptr(FuFirmware) firmware = fu_cfu_offer_new();

	g_assert_true(FU_IS_CFU_OFFER(firmware));

	/* everything is zero until set */
	g_assert_cmpint(fu_cfu_offer_get_segment_number(FU_CFU_OFFER(firmware)), ==, 0x0);
	g_assert_false(fu_cfu_offer_get_force_immediate_reset(FU_CFU_OFFER(firmware)));
	g_assert_false(fu_cfu_offer_get_force_ignore_version(FU_CFU_OFFER(firmware)));
	g_assert_cmpint(fu_cfu_offer_get_component_id(FU_CFU_OFFER(firmware)), ==, 0x0);
	g_assert_cmpint(fu_cfu_offer_get_token(FU_CFU_OFFER(firmware)), ==, 0x0);
	g_assert_cmpint(fu_cfu_offer_get_hw_variant(FU_CFU_OFFER(firmware)), ==, 0x0);
	g_assert_cmpint(fu_cfu_offer_get_protocol_revision(FU_CFU_OFFER(firmware)), ==, 0x0);
	g_assert_cmpint(fu_cfu_offer_get_bank(FU_CFU_OFFER(firmware)), ==, 0x0);
	g_assert_cmpint(fu_cfu_offer_get_milestone(FU_CFU_OFFER(firmware)), ==, 0x0);
	g_assert_cmpint(fu_cfu_offer_get_product_id(FU_CFU_OFFER(firmware)), ==, 0x0);

	/* the offer is never something we want to auto-detect */
	g_assert_true(fu_firmware_has_flag(firmware, FU_FIRMWARE_FLAG_NO_AUTO_DETECTION));
	g_assert_true(fu_firmware_has_flag(firmware, FU_FIRMWARE_FLAG_HAS_VID_PID));
	g_assert_cmpint(fu_firmware_get_version_format(firmware), ==, FWUPD_VERSION_FORMAT_SURFACE);
}

static void
fu_cfu_offer_setters_func(void)
{
	g_autoptr(FuFirmware) firmware = fu_cfu_offer_new();
	FuCfuOffer *self = FU_CFU_OFFER(firmware);

	fu_cfu_offer_set_segment_number(self, 0x42);
	fu_cfu_offer_set_force_immediate_reset(self, TRUE);
	fu_cfu_offer_set_force_ignore_version(self, TRUE);
	fu_cfu_offer_set_component_id(self, 0xAB);
	fu_cfu_offer_set_token(self, 0xCD);
	fu_cfu_offer_set_hw_variant(self, 0xFACEBEEF);
	fu_cfu_offer_set_protocol_revision(self, 0xF);
	fu_cfu_offer_set_bank(self, 0x3);
	fu_cfu_offer_set_milestone(self, 0x7);
	fu_cfu_offer_set_product_id(self, 0xDEAD);

	g_assert_cmpint(fu_cfu_offer_get_segment_number(self), ==, 0x42);
	g_assert_true(fu_cfu_offer_get_force_immediate_reset(self));
	g_assert_true(fu_cfu_offer_get_force_ignore_version(self));
	g_assert_cmpint(fu_cfu_offer_get_component_id(self), ==, 0xAB);
	g_assert_cmpint(fu_cfu_offer_get_token(self), ==, 0xCD);
	g_assert_cmpint(fu_cfu_offer_get_hw_variant(self), ==, 0xFACEBEEF);
	g_assert_cmpint(fu_cfu_offer_get_protocol_revision(self), ==, 0xF);
	g_assert_cmpint(fu_cfu_offer_get_bank(self), ==, 0x3);
	g_assert_cmpint(fu_cfu_offer_get_milestone(self), ==, 0x7);
	g_assert_cmpint(fu_cfu_offer_get_product_id(self), ==, 0xDEAD);
}

/*
 * Several of the offer properties are packed into bitfields when written, and
 * unpacked again when parsed -- check that every field survives the trip, and
 * that the widest legal value of each bitfield does not bleed into a neighbor.
 */
static void
fu_cfu_offer_write_parse_func(void)
{
	gboolean ret;
	g_autoptr(FuFirmware) firmware1 = fu_cfu_offer_new();
	g_autoptr(FuFirmware) firmware2 = fu_cfu_offer_new();
	g_autoptr(GBytes) blob = NULL;
	g_autoptr(GError) error = NULL;

	fu_cfu_offer_set_segment_number(FU_CFU_OFFER(firmware1), 0x42);
	fu_cfu_offer_set_force_immediate_reset(FU_CFU_OFFER(firmware1), TRUE);
	fu_cfu_offer_set_force_ignore_version(FU_CFU_OFFER(firmware1), TRUE);
	fu_cfu_offer_set_component_id(FU_CFU_OFFER(firmware1), 0xAB);
	fu_cfu_offer_set_token(FU_CFU_OFFER(firmware1), 0xCD);
	fu_cfu_offer_set_hw_variant(FU_CFU_OFFER(firmware1), 0xFACEBEEF);
	fu_cfu_offer_set_protocol_revision(FU_CFU_OFFER(firmware1), 0xF);
	fu_cfu_offer_set_bank(FU_CFU_OFFER(firmware1), 0x3);
	fu_cfu_offer_set_milestone(FU_CFU_OFFER(firmware1), 0x7);
	fu_cfu_offer_set_product_id(FU_CFU_OFFER(firmware1), 0xDEAD);
	fu_firmware_set_version_raw(firmware1, 0x01234567);

	blob = fu_firmware_write(firmware1, &error);
	g_assert_no_error(error);
	g_assert_nonnull(blob);
	g_assert_cmpint(g_bytes_get_size(blob), ==, FU_CFU_OFFER_TEST_SIZE);

	ret =
	    fu_firmware_parse_bytes(firmware2, blob, 0x0, FU_FIRMWARE_PARSE_FLAG_NO_SEARCH, &error);
	g_assert_no_error(error);
	g_assert_true(ret);

	g_assert_cmpint(fu_cfu_offer_get_segment_number(FU_CFU_OFFER(firmware2)), ==, 0x42);
	g_assert_true(fu_cfu_offer_get_force_immediate_reset(FU_CFU_OFFER(firmware2)));
	g_assert_true(fu_cfu_offer_get_force_ignore_version(FU_CFU_OFFER(firmware2)));
	g_assert_cmpint(fu_cfu_offer_get_component_id(FU_CFU_OFFER(firmware2)), ==, 0xAB);
	g_assert_cmpint(fu_cfu_offer_get_token(FU_CFU_OFFER(firmware2)), ==, 0xCD);
	g_assert_cmpint(fu_cfu_offer_get_hw_variant(FU_CFU_OFFER(firmware2)), ==, 0xFACEBEEF);
	g_assert_cmpint(fu_cfu_offer_get_protocol_revision(FU_CFU_OFFER(firmware2)), ==, 0xF);
	g_assert_cmpint(fu_cfu_offer_get_bank(FU_CFU_OFFER(firmware2)), ==, 0x3);
	g_assert_cmpint(fu_cfu_offer_get_milestone(FU_CFU_OFFER(firmware2)), ==, 0x7);
	g_assert_cmpint(fu_cfu_offer_get_product_id(FU_CFU_OFFER(firmware2)), ==, 0xDEAD);
	g_assert_cmpint(fu_firmware_get_version_raw(firmware2), ==, 0x01234567);
}

/*
 * The two boolean flags share a byte -- check they are independent, as a
 * mask-vs-shift mistake would make one imply the other.
 */
static void
fu_cfu_offer_flags_independent_func(void)
{
	struct {
		gboolean force_immediate_reset;
		gboolean force_ignore_version;
	} map[] = {
	    {FALSE, FALSE},
	    {TRUE, FALSE},
	    {FALSE, TRUE},
	    {TRUE, TRUE},
	};
	for (guint i = 0; i < G_N_ELEMENTS(map); i++) {
		gboolean ret;
		g_autoptr(FuFirmware) firmware1 = fu_cfu_offer_new();
		g_autoptr(FuFirmware) firmware2 = fu_cfu_offer_new();
		g_autoptr(GBytes) blob = NULL;
		g_autoptr(GError) error = NULL;

		fu_cfu_offer_set_force_immediate_reset(FU_CFU_OFFER(firmware1),
						       map[i].force_immediate_reset);
		fu_cfu_offer_set_force_ignore_version(FU_CFU_OFFER(firmware1),
						      map[i].force_ignore_version);
		blob = fu_firmware_write(firmware1, &error);
		g_assert_no_error(error);
		g_assert_nonnull(blob);
		ret = fu_firmware_parse_bytes(firmware2,
					      blob,
					      0x0,
					      FU_FIRMWARE_PARSE_FLAG_NO_SEARCH,
					      &error);
		g_assert_no_error(error);
		g_assert_true(ret);
		g_assert_cmpint(fu_cfu_offer_get_force_immediate_reset(FU_CFU_OFFER(firmware2)),
				==,
				map[i].force_immediate_reset);
		g_assert_cmpint(fu_cfu_offer_get_force_ignore_version(FU_CFU_OFFER(firmware2)),
				==,
				map[i].force_ignore_version);
	}
}

static void
fu_cfu_offer_parse_truncated_func(void)
{
	gboolean ret;
	g_autoptr(FuFirmware) firmware = fu_cfu_offer_new();
	g_autoptr(GBytes) blob = NULL;
	g_autoptr(GError) error = NULL;
	const guint8 buf[FU_CFU_OFFER_TEST_SIZE - 1] = {0x0};

	blob = g_bytes_new_static(buf, sizeof(buf));
	ret =
	    fu_firmware_parse_bytes(firmware, blob, 0x0, FU_FIRMWARE_PARSE_FLAG_NO_SEARCH, &error);
	g_assert_error(error, FWUPD_ERROR, FWUPD_ERROR_INVALID_DATA);
	g_assert_false(ret);
}

static void
fu_cfu_offer_version_func(void)
{
	g_autofree gchar *version = NULL;
	g_autoptr(FuFirmware) firmware = fu_cfu_offer_new();

	/* the CFU offer uses the Surface version format, i.e. AA.BBCC.DD */
	fu_firmware_set_version_raw(firmware, 0x01234567);
	version = fu_firmware_get_version(firmware) != NULL
		      ? g_strdup(fu_firmware_get_version(firmware))
		      : NULL;
	g_assert_cmpstr(version, ==, "1.9029.103");
}

static void
fu_cfu_offer_export_func(void)
{
	g_autofree gchar *xml = NULL;
	g_autoptr(FuFirmware) firmware = fu_cfu_offer_new();
	g_autoptr(GError) error = NULL;

	fu_cfu_offer_set_segment_number(FU_CFU_OFFER(firmware), 0x42);
	fu_cfu_offer_set_force_immediate_reset(FU_CFU_OFFER(firmware), TRUE);
	fu_cfu_offer_set_component_id(FU_CFU_OFFER(firmware), 0xAB);
	fu_cfu_offer_set_product_id(FU_CFU_OFFER(firmware), 0xDEAD);

	xml = fu_firmware_export_to_xml(firmware, FU_FIRMWARE_EXPORT_FLAG_NONE, &error);
	g_assert_no_error(error);
	g_assert_nonnull(xml);
	g_assert_nonnull(g_strstr_len(xml, -1, "<segment_number>0x42</segment_number>"));
	g_assert_nonnull(
	    g_strstr_len(xml, -1, "<force_immediate_reset>true</force_immediate_reset>"));
	g_assert_nonnull(g_strstr_len(xml, -1, "<component_id>0xab</component_id>"));
	g_assert_nonnull(g_strstr_len(xml, -1, "<product_id>0xdead</product_id>"));
}

static void
fu_cfu_payload_func(void)
{
	gboolean ret;
	g_autoptr(FuChunk) chk1 = NULL;
	g_autoptr(FuChunk) chk2 = NULL;
	g_autoptr(FuFirmware) firmware1 = fu_cfu_payload_new();
	g_autoptr(FuFirmware) firmware2 = fu_cfu_payload_new();
	g_autoptr(GBytes) blob = NULL;
	g_autoptr(GBytes) blob1 = g_bytes_new_static("hello world", 11);
	g_autoptr(GBytes) blob2 = g_bytes_new_static("there world", 11);
	g_autoptr(GError) error = NULL;
	g_autoptr(GBytes) blob1_parsed = NULL;
	g_autoptr(GBytes) blob2_parsed = NULL;
	g_autoptr(GPtrArray) chunks = NULL;

	g_assert_true(FU_IS_CFU_PAYLOAD(firmware1));
	g_assert_true(fu_firmware_has_flag(firmware1, FU_FIRMWARE_FLAG_NO_AUTO_DETECTION));

	chk1 = fu_chunk_bytes_new(blob1);
	fu_chunk_set_address(chk1, 0x08001234);
	fu_firmware_add_chunk(firmware1, chk1);
	chk2 = fu_chunk_bytes_new(blob2);
	fu_chunk_set_address(chk2, 0x08005678);
	fu_firmware_add_chunk(firmware1, chk2);

	/* each chunk is a 5 byte header plus the payload */
	blob = fu_firmware_write(firmware1, &error);
	g_assert_no_error(error);
	g_assert_nonnull(blob);
	g_assert_cmpint(g_bytes_get_size(blob), ==, (5 + 11) * 2);

	ret =
	    fu_firmware_parse_bytes(firmware2, blob, 0x0, FU_FIRMWARE_PARSE_FLAG_NO_SEARCH, &error);
	g_assert_no_error(error);
	g_assert_true(ret);

	chunks = fu_firmware_get_chunks(firmware2, &error);
	g_assert_no_error(error);
	g_assert_nonnull(chunks);
	g_assert_cmpint(chunks->len, ==, 2);
	g_assert_cmpint(fu_chunk_get_address(g_ptr_array_index(chunks, 0)), ==, 0x08001234);
	g_assert_cmpint(fu_chunk_get_data_sz(g_ptr_array_index(chunks, 0)), ==, 11);
	g_assert_cmpint(fu_chunk_get_address(g_ptr_array_index(chunks, 1)), ==, 0x08005678);
	g_assert_cmpint(fu_chunk_get_data_sz(g_ptr_array_index(chunks, 1)), ==, 11);

	/* the payload data survives the trip, not just the size */
	blob1_parsed = fu_chunk_get_bytes(g_ptr_array_index(chunks, 0));
	ret = fu_bytes_compare(blob1_parsed, blob1, &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	blob2_parsed = fu_chunk_get_bytes(g_ptr_array_index(chunks, 1));
	ret = fu_bytes_compare(blob2_parsed, blob2, &error);
	g_assert_no_error(error);
	g_assert_true(ret);
}

/*
 * A chunk header declaring a zero length would make the parser loop forever,
 * so it has to be rejected.
 */
static void
fu_cfu_payload_zero_size_func(void)
{
	gboolean ret;
	g_autoptr(FuFirmware) firmware = fu_cfu_payload_new();
	g_autoptr(GBytes) blob = NULL;
	g_autoptr(GError) error = NULL;
	const guint8 buf[] = {0x34, 0x12, 0x00, 0x08, 0x00};

	blob = g_bytes_new_static(buf, sizeof(buf));
	ret =
	    fu_firmware_parse_bytes(firmware, blob, 0x0, FU_FIRMWARE_PARSE_FLAG_NO_SEARCH, &error);
	g_assert_error(error, FWUPD_ERROR, FWUPD_ERROR_INVALID_DATA);
	g_assert_false(ret);
}

int
main(int argc, char **argv)
{
	(void)g_setenv("G_TEST_SRCDIR", SRCDIR, FALSE);
	g_test_init(&argc, &argv, NULL);
	g_test_add_func("/fwupd/cfu-offer/default", fu_cfu_offer_default_func);
	g_test_add_func("/fwupd/cfu-offer/setters", fu_cfu_offer_setters_func);
	g_test_add_func("/fwupd/cfu-offer/write-parse", fu_cfu_offer_write_parse_func);
	g_test_add_func("/fwupd/cfu-offer/flags-independent", fu_cfu_offer_flags_independent_func);
	g_test_add_func("/fwupd/cfu-offer/parse-truncated", fu_cfu_offer_parse_truncated_func);
	g_test_add_func("/fwupd/cfu-offer/version", fu_cfu_offer_version_func);
	g_test_add_func("/fwupd/cfu-offer/export", fu_cfu_offer_export_func);
	g_test_add_func("/fwupd/cfu-payload", fu_cfu_payload_func);
	g_test_add_func("/fwupd/cfu-payload/zero-size", fu_cfu_payload_zero_size_func);
	return g_test_run();
}
