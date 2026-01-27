/*
 * Copyright 2017 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include <fwupdplugin.h>

#include "fu-context-private.h"

static void
fu_firmware_raw_aligned_func(void)
{
	gboolean ret;
	g_autoptr(FuFirmware) firmware1 = fu_firmware_new();
	g_autoptr(FuFirmware) firmware2 = fu_firmware_new();
	g_autoptr(GError) error = NULL;
	g_autoptr(GBytes) blob = g_bytes_new_static("hello", 5);

	/* no alignment */
	ret =
	    fu_firmware_parse_bytes(firmware1, blob, 0x0, FU_FIRMWARE_PARSE_FLAG_NO_SEARCH, &error);
	g_assert_no_error(error);
	g_assert_true(ret);

	/* invalid alignment */
	fu_firmware_set_alignment(firmware2, FU_FIRMWARE_ALIGNMENT_4K);
	ret =
	    fu_firmware_parse_bytes(firmware2, blob, 0x0, FU_FIRMWARE_PARSE_FLAG_NO_SEARCH, &error);
	g_assert_error(error, FWUPD_ERROR, FWUPD_ERROR_INVALID_FILE);
	g_assert_false(ret);
}

static void
fu_firmware_srec_func(void)
{
	g_autofree gchar *filename = NULL;
	g_autoptr(FuFirmware) firmware = NULL;
	g_autoptr(GBytes) data_bin = NULL;
	g_autoptr(GError) error = NULL;

	filename = g_test_build_filename(G_TEST_DIST, "tests", "srec.builder.xml", NULL);
	firmware = fu_firmware_new_from_filename(filename, &error);
	g_assert_no_error(error);
	g_assert_nonnull(firmware);
	data_bin = fu_firmware_get_bytes(firmware, &error);
	g_assert_no_error(error);
	g_assert_nonnull(data_bin);
	g_assert_cmpint(g_bytes_get_size(data_bin), ==, 11);
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
	g_autoptr(FuFirmware) firmware = NULL;
	g_autoptr(FuFirmware) img1 = NULL;
	g_autoptr(FuFdtImage) img2 = NULL;
	g_autoptr(GError) error = NULL;

	filename = g_test_build_filename(G_TEST_DIST, "tests", "fdt.builder.xml", NULL);
	firmware = fu_firmware_new_from_filename(filename, &error);
	g_assert_no_error(error);
	g_assert_nonnull(firmware);
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
	g_assert_error(error, FWUPD_ERROR, FWUPD_ERROR_INVALID_DATA);
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
	g_autoptr(FuFirmware) firmware = NULL;
	g_autoptr(GError) error = NULL;

	filename = g_test_build_filename(G_TEST_DIST, "tests", "fit.builder.xml", NULL);
	firmware = fu_firmware_new_from_filename(filename, &error);
	g_assert_no_error(error);
	g_assert_nonnull(firmware);
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
	g_autoptr(GInputStream) stream = NULL;
	g_autoptr(GError) error = NULL;
	const gchar *buf = "S3060000001400E5\r\n"
			   "S31000000002281102000000007F0304002C\r\n"
			   "S306000000145095\r\n"
			   "S70500000000FA\r\n";
	data_srec = g_bytes_new_static(buf, strlen(buf));
	g_assert_no_error(error);
	g_assert_nonnull(data_srec);
	stream = g_memory_input_stream_new_from_bytes(data_srec);
	ret = fu_firmware_tokenize(firmware, stream, FU_FIRMWARE_PARSE_FLAG_NONE, &error);
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
	fu_firmware_add_image_gtype(firmware, FU_TYPE_FIRMWARE);
	ret = fu_firmware_build(firmware, n, &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	g_assert_cmpstr(fu_firmware_get_version(firmware), ==, "1.2.3");

	/* verify image */
	img = fu_firmware_get_image_by_id(firmware, "xxx|h?ad*", &error);
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

/* nocheck:name */
static gsize
fu_dfuse_firmware_image_get_size(FuFirmware *firmware)
{
	g_autoptr(GPtrArray) chunks = fu_firmware_get_chunks(firmware, NULL);
	gsize length = 0;
	for (guint i = 0; i < chunks->len; i++) {
		FuChunk *chk = g_ptr_array_index(chunks, i);
		length += fu_chunk_get_data_sz(chk);
	}
	return length;
}

/* nocheck:name */
static gsize
fu_dfuse_firmware_get_size(FuFirmware *firmware)
{
	gsize length = 0;
	g_autoptr(GPtrArray) images = fu_firmware_get_images(firmware);
	for (guint i = 0; i < images->len; i++) {
		FuFirmware *image = g_ptr_array_index(images, i);
		length += fu_dfuse_firmware_image_get_size(image);
	}
	return length;
}

static void
fu_firmware_dfuse_func(void)
{
	g_autofree gchar *filename = NULL;
	g_autoptr(FuFirmware) firmware = NULL;
	g_autoptr(GError) error = NULL;

	/* load a DfuSe firmware */
	filename = g_test_build_filename(G_TEST_DIST, "tests", "dfuse.builder.xml", NULL);
	g_assert_nonnull(filename);
	firmware = fu_firmware_new_from_filename(filename, &error);
	g_assert_no_error(error);
	g_assert_nonnull(firmware);
	g_assert_cmpint(fu_dfu_firmware_get_vid(FU_DFU_FIRMWARE(firmware)), ==, 0x1234);
	g_assert_cmpint(fu_dfu_firmware_get_pid(FU_DFU_FIRMWARE(firmware)), ==, 0x5678);
	g_assert_cmpint(fu_dfu_firmware_get_release(FU_DFU_FIRMWARE(firmware)), ==, 0x8642);
	g_assert_cmpint(fu_dfuse_firmware_get_size(firmware), ==, 0x21);
}

static void
fu_firmware_fmap_func(void)
{
	g_autofree gchar *filename = NULL;
	g_autofree gchar *csum = NULL;
	g_autofree gchar *img_str = NULL;
	g_autoptr(FuFirmware) firmware = NULL;
	g_autoptr(FuFirmware) img = NULL;
	g_autoptr(GBytes) img_blob = NULL;
	g_autoptr(GBytes) roundtrip = NULL;
	g_autoptr(GError) error = NULL;
	g_autoptr(GPtrArray) images = NULL;

#ifndef HAVE_MEMMEM
	g_test_skip("no memmem()");
	return;
#endif

	/* load firmware */
	filename = g_test_build_filename(G_TEST_DIST, "tests", "fmap-offset.builder.xml", NULL);
	g_assert_nonnull(filename);
	firmware = fu_firmware_new_from_filename(filename, &error);
	g_assert_no_error(error);
	g_assert_nonnull(firmware);

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
	csum = g_compute_checksum_for_bytes(G_CHECKSUM_SHA256, roundtrip);
	g_assert_cmpstr(csum,
			==,
			"229fcd952264f42ae4853eda7e716cc5c1ae18e7f804a6ba39ab1dfde5737d7e");
}

static void
fu_firmware_sorted_func(void)
{
	gboolean ret;
	g_autofree gchar *xml1 = NULL;
	g_autofree gchar *xml2 = NULL;
	g_autoptr(FuFirmware) firmware1 = fu_firmware_new();
	g_autoptr(FuFirmware) firmware2 = fu_firmware_new();
	g_autoptr(FuFirmware) firmware3 = fu_firmware_new();
	g_autoptr(FuFirmware) firmware = fu_firmware_new();
	g_autoptr(GError) error = NULL;

	fu_firmware_set_id(firmware1, "zzz");
	fu_firmware_set_id(firmware2, "aaa");
	fu_firmware_set_id(firmware3, "bbb");

	fu_firmware_set_idx(firmware1, 0x999);
	fu_firmware_set_idx(firmware2, 0x200);
	fu_firmware_set_idx(firmware3, 0x100);

	fu_firmware_add_image_gtype(firmware, FU_TYPE_FIRMWARE);
	ret = fu_firmware_add_image(firmware, firmware1, &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	ret = fu_firmware_add_image(firmware, firmware2, &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	ret = fu_firmware_add_image(firmware, firmware3, &error);
	g_assert_no_error(error);
	g_assert_true(ret);

	/* by idx */
	fu_firmware_add_flag(firmware, FU_FIRMWARE_FLAG_DEDUPE_IDX);
	xml1 = fu_firmware_export_to_xml(firmware, FU_FIRMWARE_EXPORT_FLAG_SORTED, &error);
	g_assert_no_error(error);
	g_debug("%s", xml1);
	g_assert_cmpstr(xml1,
			==,
			"<firmware>\n"
			"  <flags>dedupe-idx</flags>\n"
			"  <firmware>\n"
			"    <id>bbb</id>\n"
			"    <idx>0x100</idx>\n"
			"  </firmware>\n"
			"  <firmware>\n"
			"    <id>aaa</id>\n"
			"    <idx>0x200</idx>\n"
			"  </firmware>\n"
			"  <firmware>\n"
			"    <id>zzz</id>\n"
			"    <idx>0x999</idx>\n"
			"  </firmware>\n"
			"</firmware>\n");

	/* now by both, here using id as it is last */
	fu_firmware_add_flag(firmware, FU_FIRMWARE_FLAG_DEDUPE_ID);
	xml2 = fu_firmware_export_to_xml(firmware, FU_FIRMWARE_EXPORT_FLAG_SORTED, &error);
	g_assert_no_error(error);
	g_debug("%s", xml2);
	g_assert_cmpstr(xml2,
			==,
			"<firmware>\n"
			"  <flags>dedupe-id,dedupe-idx</flags>\n"
			"  <firmware>\n"
			"    <id>aaa</id>\n"
			"    <idx>0x200</idx>\n"
			"  </firmware>\n"
			"  <firmware>\n"
			"    <id>bbb</id>\n"
			"    <idx>0x100</idx>\n"
			"  </firmware>\n"
			"  <firmware>\n"
			"    <id>zzz</id>\n"
			"    <idx>0x999</idx>\n"
			"  </firmware>\n"
			"</firmware>\n");
}

static void
fu_firmware_new_from_gtypes_func(void)
{
	g_autofree gchar *filename = NULL;
	g_autoptr(FuFirmware) firmware = NULL;
	g_autoptr(FuFirmware) firmware1 = NULL;
	g_autoptr(FuFirmware) firmware2 = NULL;
	g_autoptr(FuFirmware) firmware3 = NULL;
	g_autoptr(GBytes) fw = NULL;
	g_autoptr(GInputStream) stream = NULL;
	g_autoptr(GError) error = NULL;

	filename = g_test_build_filename(G_TEST_DIST, "tests", "dfu.builder.xml", NULL);
	firmware = fu_firmware_new_from_filename(filename, &error);
	g_assert_no_error(error);
	g_assert_nonnull(firmware);
	fw = fu_firmware_write(firmware, &error);
	g_assert_no_error(error);
	g_assert_nonnull(fw);
	stream = g_memory_input_stream_new_from_bytes(fw);
	g_assert_no_error(error);
	g_assert_nonnull(stream);

	/* dfu -> FuDfuFirmware */
	firmware1 = fu_firmware_new_from_gtypes(stream,
						0x0,
						FU_FIRMWARE_PARSE_FLAG_NONE,
						&error,
						FU_TYPE_SREC_FIRMWARE,
						FU_TYPE_DFUSE_FIRMWARE,
						FU_TYPE_DFU_FIRMWARE,
						G_TYPE_INVALID);
	g_assert_no_error(error);
	g_assert_nonnull(firmware1);
	g_assert_cmpstr(G_OBJECT_TYPE_NAME(firmware1), ==, "FuDfuFirmware");

	/* dfu -> FuFirmware */
	firmware2 = fu_firmware_new_from_gtypes(stream,
						0x0,
						FU_FIRMWARE_PARSE_FLAG_NONE,
						&error,
						FU_TYPE_SREC_FIRMWARE,
						FU_TYPE_FIRMWARE,
						G_TYPE_INVALID);
	g_assert_no_error(error);
	g_assert_nonnull(firmware2);
	g_assert_cmpstr(G_OBJECT_TYPE_NAME(firmware2), ==, "FuFirmware");

	/* dfu -> error */
	firmware3 = fu_firmware_new_from_gtypes(stream,
						0x0,
						FU_FIRMWARE_PARSE_FLAG_NONE,
						&error,
						FU_TYPE_SREC_FIRMWARE,
						G_TYPE_INVALID);
	g_assert_error(error, FWUPD_ERROR, FWUPD_ERROR_INVALID_FILE);
	g_assert_null(firmware3);
}

static void
fu_firmware_csv_func(void)
{
	FuCsvEntry *entry_tmp;
	gboolean ret;
	g_autofree gchar *str = NULL;
	g_autoptr(FuFirmware) firmware = fu_csv_firmware_new();
	g_autoptr(GBytes) blob = NULL;
	g_autoptr(GError) error = NULL;
	g_autoptr(GPtrArray) imgs = NULL;
	const gchar *data =
	    "sbat,1,SBAT Version,sbat,1,https://github.com/rhboot/shim/blob/main/SBAT.md\n"
	    "grub,1,Free Software Foundation,grub,2.04,https://www.gnu.org/software/grub/\n";

	fu_csv_firmware_add_column_id(FU_CSV_FIRMWARE(firmware), "$id");
	fu_csv_firmware_add_column_id(FU_CSV_FIRMWARE(firmware), "component_generation");
	fu_csv_firmware_add_column_id(FU_CSV_FIRMWARE(firmware), "vendor_name");
	fu_csv_firmware_add_column_id(FU_CSV_FIRMWARE(firmware), "vendor_package_name");
	fu_csv_firmware_add_column_id(FU_CSV_FIRMWARE(firmware), "vendor_version");
	fu_csv_firmware_add_column_id(FU_CSV_FIRMWARE(firmware), "vendor_url");

	g_assert_cmpstr(fu_csv_firmware_get_column_id(FU_CSV_FIRMWARE(firmware), 0), ==, "$id");
	g_assert_cmpstr(fu_csv_firmware_get_column_id(FU_CSV_FIRMWARE(firmware), 1),
			==,
			"component_generation");
	g_assert_cmpstr(fu_csv_firmware_get_column_id(FU_CSV_FIRMWARE(firmware), 5),
			==,
			"vendor_url");
	g_assert_cmpstr(fu_csv_firmware_get_column_id(FU_CSV_FIRMWARE(firmware), 6), ==, NULL);

	blob = g_bytes_new(data, strlen(data));
	ret = fu_firmware_parse_bytes(firmware, blob, 0x0, FU_FIRMWARE_PARSE_FLAG_NONE, &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	str = fu_firmware_to_string(firmware);
	g_debug("%s", str);

	imgs = fu_firmware_get_images(firmware);
	g_assert_cmpint(imgs->len, ==, 2);

	entry_tmp = g_ptr_array_index(imgs, 1);

	g_assert_cmpstr(fu_firmware_get_id(FU_FIRMWARE(entry_tmp)), ==, "grub");
	g_assert_cmpstr(fu_csv_entry_get_value_by_idx(entry_tmp, 0), ==, "grub");
	g_assert_cmpstr(fu_csv_entry_get_value_by_idx(entry_tmp, 1), ==, "1");
	g_assert_cmpstr(fu_csv_entry_get_value_by_column_id(entry_tmp, "vendor_version"),
			==,
			"2.04");
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
	ret = fu_firmware_add_image(firmware1, img1, &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	fu_firmware_set_bytes(img2, blob2);
	ret = fu_firmware_add_image(firmware1, img2, &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	blob3 = fu_firmware_write(firmware1, &error);
	g_assert_no_error(error);
	g_assert_nonnull(blob3);
	g_assert_cmpint(g_bytes_get_size(blob3), ==, 1024);

	/* parse them back */
	ret = fu_firmware_parse_bytes(firmware2,
				      blob3,
				      0x0,
				      FU_FIRMWARE_PARSE_FLAG_NO_SEARCH,
				      &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	str = fu_firmware_to_string(firmware2);
	g_debug("%s", str);

	/* verify we got both images */
	imgs = fu_firmware_get_images(firmware2);
	g_assert_cmpint(imgs->len, ==, 2);
}

static void
fu_firmware_dfu_func(void)
{
	g_autofree gchar *filename = NULL;
	g_autoptr(FuFirmware) firmware = NULL;
	g_autoptr(GBytes) data_bin = NULL;
	g_autoptr(GError) error = NULL;

	filename = g_test_build_filename(G_TEST_DIST, "tests", "dfu.builder.xml", NULL);
	firmware = fu_firmware_new_from_filename(filename, &error);
	g_assert_no_error(error);
	g_assert_nonnull(firmware);
	g_assert_cmpint(fu_dfu_firmware_get_vid(FU_DFU_FIRMWARE(firmware)), ==, 0x1234);
	g_assert_cmpint(fu_dfu_firmware_get_pid(FU_DFU_FIRMWARE(firmware)), ==, 0x4321);
	g_assert_cmpint(fu_dfu_firmware_get_release(FU_DFU_FIRMWARE(firmware)), ==, 0xdead);
	data_bin = fu_firmware_get_bytes(firmware, &error);
	g_assert_no_error(error);
	g_assert_nonnull(data_bin);
	g_assert_cmpint(g_bytes_get_size(data_bin), ==, 12);
}

static void
fu_firmware_ifwi_cpd_func(void)
{
	g_autofree gchar *filename = NULL;
	g_autoptr(FuFirmware) firmware = NULL;
	g_autoptr(FuFirmware) img1 = NULL;
	g_autoptr(FuFirmware) img2 = NULL;
	g_autoptr(GBytes) data_bin = NULL;
	g_autoptr(GError) error = NULL;

	filename = g_test_build_filename(G_TEST_DIST, "tests", "ifwi-cpd.builder.xml", NULL);
	firmware = fu_firmware_new_from_filename(filename, &error);
	g_assert_no_error(error);
	g_assert_nonnull(firmware);
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
	g_autofree gchar *filename = NULL;
	g_autoptr(FuFirmware) firmware = NULL;
	g_autoptr(FuFirmware) img1 = NULL;
	g_autoptr(FuFirmware) img2 = NULL;
	g_autoptr(GBytes) data_bin = NULL;
	g_autoptr(GError) error = NULL;

	filename = g_test_build_filename(G_TEST_DIST, "tests", "ifwi-fpt.builder.xml", NULL);
	firmware = fu_firmware_new_from_filename(filename, &error);
	g_assert_no_error(error);
	g_assert_nonnull(firmware);
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
	g_autofree gchar *filename = NULL;
	g_autoptr(FuFirmware) firmware1 = NULL;
	g_autoptr(FuFirmware) firmware2 = fu_oprom_firmware_new();
	g_autoptr(FuFirmware) img1 = NULL;
	g_autoptr(GBytes) data_bin = NULL;
	g_autoptr(GError) error = NULL;

	filename = g_test_build_filename(G_TEST_DIST, "tests", "oprom.builder.xml", NULL);
	firmware1 = fu_firmware_new_from_filename(filename, &error);
	g_assert_no_error(error);
	g_assert_nonnull(firmware1);
	g_assert_cmpint(fu_firmware_get_idx(firmware1), ==, 0x1);
	data_bin = fu_firmware_write(firmware1, &error);
	g_assert_no_error(error);
	g_assert_nonnull(data_bin);
	g_assert_cmpint(g_bytes_get_size(data_bin), ==, 1024);

	/* re-parse to get the CPD image */
	ret = fu_firmware_parse_bytes(firmware2,
				      data_bin,
				      0x0,
				      FU_FIRMWARE_PARSE_FLAG_CACHE_STREAM,
				      &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	img1 = fu_firmware_get_image_by_id(firmware2, "cpd", &error);
	g_assert_no_error(error);
	g_assert_nonnull(img1);
	g_assert_cmpint(fu_firmware_get_offset(img1), ==, 512);
	g_assert_cmpint(fu_firmware_get_size(img1), ==, 512);
}

static void
fu_firmware_dfu_patch_func(void)
{
	g_autofree gchar *csum = NULL;
	g_autofree gchar *filename = NULL;
	g_autoptr(FuFirmware) firmware = NULL;
	g_autoptr(GBytes) data_new = NULL;
	g_autoptr(GBytes) data_patch0 = g_bytes_new_static("XXXX", 4);
	g_autoptr(GBytes) data_patch1 = g_bytes_new_static("HELO", 4);
	g_autoptr(GError) error = NULL;

	filename = g_test_build_filename(G_TEST_DIST, "tests", "dfu.builder.xml", NULL);
	firmware = fu_firmware_new_from_filename(filename, &error);
	g_assert_no_error(error);
	g_assert_nonnull(firmware);

	/* add a couple of patches */
	fu_firmware_add_patch(firmware, 0x0, data_patch0);
	fu_firmware_add_patch(firmware, 0x0, data_patch1);
	fu_firmware_add_patch(firmware, 0x8, data_patch1);

	data_new = fu_firmware_write(firmware, &error);
	g_assert_no_error(error);
	g_assert_nonnull(data_new);
	fu_dump_full(G_LOG_DOMAIN,
		     "patch",
		     g_bytes_get_data(data_new, NULL),
		     g_bytes_get_size(data_new),
		     20,
		     FU_DUMP_FLAG_SHOW_ASCII | FU_DUMP_FLAG_SHOW_ADDRESSES);
	csum = g_compute_checksum_for_bytes(G_CHECKSUM_SHA1, data_new);
	g_assert_cmpstr(csum, ==, "676c039e8cb1d2f51831fcb77be36db24bb8ecf8");
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

	fu_firmware_add_image_gtype(firmware, FU_TYPE_FIRMWARE);

	fu_firmware_set_addr(img1, 0x200);
	fu_firmware_set_idx(img1, 13);
	fu_firmware_set_id(img1, "primary");
	fu_firmware_set_filename(img1, "BIOS.bin");
	ret = fu_firmware_add_image(firmware, img1, &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	fu_firmware_set_addr(img2, 0x400);
	fu_firmware_set_idx(img2, 23);
	fu_firmware_set_id(img2, "secondary");
	ret = fu_firmware_add_image(firmware, img2, &error);
	g_assert_no_error(error);
	g_assert_true(ret);

	/* check depth */
	g_assert_cmpint(fu_firmware_get_depth(firmware), ==, 0);
	g_assert_cmpint(fu_firmware_get_depth(img1), ==, 1);
	g_assert_cmpint(fu_firmware_get_depth(img2), ==, 1);

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
			"  <image_gtypes>\n"
			"    <gtype>FuFirmware</gtype>\n"
			"  </image_gtypes>\n"
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
fu_firmware_convert_version_func(void)
{
	g_autoptr(FuFirmware) firmware = fu_intel_thunderbolt_nvm_new();
	fu_firmware_set_version_raw(firmware, 0x1234);
	g_assert_cmpstr(fu_firmware_get_version(firmware), ==, "12.34");
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
	g_assert_error(error, FWUPD_ERROR, FWUPD_ERROR_INVALID_DATA);
	g_assert_false(ret);
}

static void
fu_firmware_dedupe_func(void)
{
	gboolean ret;
	g_autoptr(FuFirmware) firmware = fu_firmware_new();
	g_autoptr(FuFirmware) img1 = fu_firmware_new();
	g_autoptr(FuFirmware) img1_old = fu_firmware_new();
	g_autoptr(FuFirmware) img2 = fu_firmware_new();
	g_autoptr(FuFirmware) img2_old = fu_firmware_new();
	g_autoptr(FuFirmware) img3 = fu_firmware_new();
	g_autoptr(FuFirmware) img_id = NULL;
	g_autoptr(FuFirmware) img_idx = NULL;
	g_autoptr(GError) error = NULL;

	fu_firmware_add_flag(firmware, FU_FIRMWARE_FLAG_DEDUPE_ID);
	fu_firmware_add_flag(firmware, FU_FIRMWARE_FLAG_DEDUPE_IDX);
	fu_firmware_add_image_gtype(firmware, FU_TYPE_FIRMWARE);
	fu_firmware_set_images_max(firmware, 2);

	fu_firmware_set_idx(img1_old, 13);
	fu_firmware_set_id(img1_old, "DAVE");
	ret = fu_firmware_add_image(firmware, img1_old, &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	g_assert_true(fu_firmware_get_parent(img1_old) == firmware);

	fu_firmware_set_idx(img1, 13);
	fu_firmware_set_id(img1, "primary");
	ret = fu_firmware_add_image(firmware, img1, &error);
	g_assert_no_error(error);
	g_assert_true(ret);

	fu_firmware_set_idx(img2_old, 123456);
	fu_firmware_set_id(img2_old, "secondary");
	ret = fu_firmware_add_image(firmware, img2_old, &error);
	g_assert_no_error(error);
	g_assert_true(ret);

	fu_firmware_set_idx(img2, 23);
	fu_firmware_set_id(img2, "secondary");
	ret = fu_firmware_add_image(firmware, img2, &error);
	g_assert_no_error(error);
	g_assert_true(ret);

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

	ret = fu_firmware_add_image(firmware, img3, &error);
	g_assert_error(error, FWUPD_ERROR, FWUPD_ERROR_INVALID_DATA);
	g_assert_false(ret);
}

static void
fu_firmware_builder_round_trip_func(void)
{
	struct {
		const gchar *xml_fn;
		const gchar *checksum;
		FuFirmwareBuilderFlags flags;
	} map[] = {
	    {
		"cab.builder.xml",
		"a708f47b1a46377f1ea420597641ffe9a40abd75",
		FU_FIRMWARE_BUILDER_FLAG_NONE,
	    },
	    {
		"cab-compressed.builder.xml",
		NULL, /* not byte-identical */
		FU_FIRMWARE_BUILDER_FLAG_NONE,
	    },
	    {
		"elf.builder.xml",
		"99ea60b8dd46085dcbf1ecd5e72b4cb73a3b6faa",
		FU_FIRMWARE_BUILDER_FLAG_NONE,
	    },
	    {
		"dfuse.builder.xml",
		"c1ff429f0e381c8fe8e1b2ee41a5a9a79e2f2ff7",
		FU_FIRMWARE_BUILDER_FLAG_NONE,
	    },
	    {
		"pefile.builder.xml",
		"73b0e0dc9f6175b7bc27b77f20e0d9eca2d2d141",
		FU_FIRMWARE_BUILDER_FLAG_NONE,
	    },
	    {
		"linear.builder.xml",
		"18fa8201652c82dc717df1905d8ab72e46e3d82b",
		FU_FIRMWARE_BUILDER_FLAG_NONE,
	    },
	    {
		"hid-report-item.builder.xml",
		"5b18c07399fc8968ce22127df38d8d923089ec92",
		FU_FIRMWARE_BUILDER_FLAG_NONE,
	    },
	    {
		"hid-descriptor.builder.xml",
		"6bb23f7c9fedc21f05528b3b63ad5837f4a16a92",
		FU_FIRMWARE_BUILDER_FLAG_NONE,
	    },
	    {
		"sbatlevel.builder.xml",
		"8204ef9477b4305748a0de6e667547cb6ce5e426",
		FU_FIRMWARE_BUILDER_FLAG_NONE,
	    },
	    {
		"csv.builder.xml",
		"986cbf8cde5bc7d8b49ee94cceae3f92efbd2eef",
		FU_FIRMWARE_BUILDER_FLAG_NONE,
	    },
	    {
		"fdt.builder.xml",
		"40f7fbaff684a6bcf67c81b3079422c2529741e1",
		FU_FIRMWARE_BUILDER_FLAG_NONE,
	    },
	    {
		"fit.builder.xml",
		"293ce07351bb7d76631c4e2ba47243db1e150f3c",
		FU_FIRMWARE_BUILDER_FLAG_NO_BINARY_COMPARE,
	    },
	    {
		"srec.builder.xml",
		"c8b405b7995d5934086c56b091a4c5df47b3b0d7",
		FU_FIRMWARE_BUILDER_FLAG_NO_BINARY_COMPARE,
	    },
	    {
		"ihex.builder.xml",
		"e7c39355f1c87a3e9bf2195a406584c5dac828bc",
		FU_FIRMWARE_BUILDER_FLAG_NONE,
	    },
#ifdef HAVE_CBOR
	    {
		"fmap.builder.xml",
		"0db91efb987353ffb779d259b130d63d1b8bcbec",
		FU_FIRMWARE_BUILDER_FLAG_NONE,
	    },
#endif
	    {
		"efi-load-option.builder.xml",
		"7ef696d22902ae97ef5f73ad9c85a28095ad56f1",
		FU_FIRMWARE_BUILDER_FLAG_NONE,
	    },
	    {
		"efi-load-option-hive.builder.xml",
		"76a378752b7ccdf3d68365d83784053356fa7e0a",
		FU_FIRMWARE_BUILDER_FLAG_NONE,
	    },
	    {
		"efi-load-option-data.builder.xml",
		"6e6190dc6b1bf45bc6e30ba7a6a98d891d692dd0",
		FU_FIRMWARE_BUILDER_FLAG_NONE,
	    },
	    {
		"edid.builder.xml",
		"64cef10b75ccce684a483d576dd4a4ce6bef8165",
		FU_FIRMWARE_BUILDER_FLAG_NONE,
	    },
	    {
		"efi-section.builder.xml",
		"a0ede7316209c536b50b6e5fb22cce8135153bc3",
		FU_FIRMWARE_BUILDER_FLAG_NONE,
	    },
	    {
		"efi-section.builder.xml",
		"a0ede7316209c536b50b6e5fb22cce8135153bc3",
		FU_FIRMWARE_BUILDER_FLAG_NONE,
	    },
	    {
		"efi-file.builder.xml",
		"90374d97cf6bc70059d24c816c188c10bd250ed7",
		FU_FIRMWARE_BUILDER_FLAG_NONE,
	    },
	    {
		"efi-filesystem.builder.xml",
		"d6fbadc1c303a3b4eede9db7fb0ddb353efffc86",
		FU_FIRMWARE_BUILDER_FLAG_NONE,
	    },
	    {
		"efi-signature.builder.xml",
		"ff7b862504262ce4853db29690b683bb06ce7d1f",
		FU_FIRMWARE_BUILDER_FLAG_NONE,
	    },
	    {
		"efi-signature-list.builder.xml",
		"450111ea0f77a0ede5b6a6305cd2e02b44b5f1e9",
		FU_FIRMWARE_BUILDER_FLAG_NONE,
	    },
	    {
		"efi-variable-authentication2.builder.xml",
		"bd08e81e9c86490dc1ffb32b1e3332606eb0fa97",
		FU_FIRMWARE_BUILDER_FLAG_NONE,
	    },
	    {
		"efi-ftw-store.builder.xml",
		"9bdb363e31e00d7fb0b42eacdc95771a3795b7ec",
		FU_FIRMWARE_BUILDER_FLAG_NONE,
	    },
	    {
		"efi-vss-auth-variable.builder.xml",
		"de6391f8b09653859b4ff93a7d5004c52c35d5c2",
		FU_FIRMWARE_BUILDER_FLAG_NONE,
	    },
	    {
		"efi-vss2-variable-store.builder.xml",
		"25ef7bf7ea600c8a739ff4dc6876bcd2f9d8d30d",
		FU_FIRMWARE_BUILDER_FLAG_NONE,
	    },
	    {
		"efi-volume.builder.xml",
		"d0f658bce79c8468458e0b64e7de24f45c063076",
		FU_FIRMWARE_BUILDER_FLAG_NONE,
	    },
	    {
		"efi-volume-sized.builder.xml",
		"d7087ea16218d700b9175a9cd0c27bd56b07a6d4",
		FU_FIRMWARE_BUILDER_FLAG_NONE,
	    },
	    {
		"ifd.builder.xml",
		"494e7be6a72e743e6738c0ecdbdcddbf27d1dbd7",
		FU_FIRMWARE_BUILDER_FLAG_NO_BINARY_COMPARE,
	    },
	    {
		"cfu-offer.builder.xml",
		"c10223887ff6cdf4475ad07c65b1f0f3a2d0d5ca",
		FU_FIRMWARE_BUILDER_FLAG_NONE,
	    },
	    {
		"cfu-payload.builder.xml",
		"5da829f5fd15a28970aed98ebb26ebf2f88ed6f2",
		FU_FIRMWARE_BUILDER_FLAG_NONE,
	    },
	    {
		"ifwi-cpd.builder.xml",
		"91e348d17cb91ef7a528e85beb39d15a0532dca5",
		FU_FIRMWARE_BUILDER_FLAG_NONE,
	    },
	    {
		"ifwi-fpt.builder.xml",
		"d1f0fb2c2a7a99441bf4a825d060642315a94d91",
		FU_FIRMWARE_BUILDER_FLAG_NONE,
	    },
	    {
		"oprom.builder.xml",
		"2e8387c1ef14ed4038e6bc637146b86b4d702fa8",
		FU_FIRMWARE_BUILDER_FLAG_NONE,
	    },
	    {
		"intel-thunderbolt.builder.xml",
		"b3a73baf05078dfdd833b407a0a6afb239ec2f23",
		FU_FIRMWARE_BUILDER_FLAG_NO_BINARY_COMPARE,
	    },
	    {
		"usb-bos-descriptor.builder.xml",
		"a305749853781c6899c4b28039cb4c7d9059b910",
		FU_FIRMWARE_BUILDER_FLAG_NONE,
	    },
	    {
		"json.builder.xml",
		"845be24c3f31c4e8f0feeadfe356b3156628ba99",
		FU_FIRMWARE_BUILDER_FLAG_NONE,
	    },
	    {
		"tpm-eventlog-v1.builder.xml",
		"79b257b9f668681e6c50f3c4c59b5430a3c56625",
		FU_FIRMWARE_BUILDER_FLAG_NONE,
	    },
	    {
		"tpm-eventlog-v2.builder.xml",
		"0b965076bd38f737aaadbaff464199ba104f719a",
		FU_FIRMWARE_BUILDER_FLAG_NONE,
	    },
#ifdef HAVE_CBOR
	    {
		"uswid.builder.xml",
		"b473fbdbe00f860c4da43f9499569394bac81f14",
		FU_FIRMWARE_BUILDER_FLAG_NONE,
	    },
	    {
		"uswid-compressed.builder.xml",
		NULL, /* not byte-identical */
		FU_FIRMWARE_BUILDER_FLAG_NONE,
	    },
#endif
	    {
		"zip.builder.xml",
		"aefdd7b205927e383981b03ded1ad22878d03263",
		FU_FIRMWARE_BUILDER_FLAG_NONE,
	    },
	    {
		"zip-compressed.builder.xml",
		"10792ff01b036ed89d11a6480694ccfd89c4d9fd",
		FU_FIRMWARE_BUILDER_FLAG_NONE,
	    },
	};
	for (guint i = 0; i < G_N_ELEMENTS(map); i++) {
		gboolean ret;
		g_autofree gchar *filename = NULL;
		g_autoptr(GError) error = NULL;

		filename = g_test_build_filename(G_TEST_DIST, "tests", map[i].xml_fn, NULL);
		g_debug("parsing: %s", filename);
		ret = fu_firmware_roundtrip_from_filename(filename,
							  map[i].checksum,
							  map[i].flags,
							  &error);
		g_assert_no_error(error);
		g_assert_true(ret);
	}
}

int
main(int argc, char **argv)
{
	g_autoptr(FuContext) ctx = fu_context_new();

	(void)g_setenv("G_TEST_SRCDIR", SRCDIR, FALSE);
	g_test_init(&argc, &argv, NULL);
	fu_context_add_firmware_gtypes(ctx);
	g_test_add_func("/fwupd/firmware", fu_firmware_func);
	g_test_add_func("/fwupd/firmware/common", fu_firmware_common_func);
	g_test_add_func("/fwupd/firmware/convert-version", fu_firmware_convert_version_func);
	g_test_add_func("/fwupd/firmware/builder-round-trip", fu_firmware_builder_round_trip_func);
	g_test_add_func("/fwupd/firmware/csv", fu_firmware_csv_func);
	g_test_add_func("/fwupd/firmware/linear", fu_firmware_linear_func);
	g_test_add_func("/fwupd/firmware/dedupe", fu_firmware_dedupe_func);
	g_test_add_func("/fwupd/firmware/build", fu_firmware_build_func);
	g_test_add_func("/fwupd/firmware/raw-aligned", fu_firmware_raw_aligned_func);
	g_test_add_func("/fwupd/firmware/srec-tokenization", fu_firmware_srec_tokenization_func);
	g_test_add_func("/fwupd/firmware/srec", fu_firmware_srec_func);
	g_test_add_func("/fwupd/firmware/fdt", fu_firmware_fdt_func);
	g_test_add_func("/fwupd/firmware/fit", fu_firmware_fit_func);
	g_test_add_func("/fwupd/firmware/ifwi-cpd", fu_firmware_ifwi_cpd_func);
	g_test_add_func("/fwupd/firmware/ifwi-fpt", fu_firmware_ifwi_fpt_func);
	g_test_add_func("/fwupd/firmware/oprom", fu_firmware_oprom_func);
	g_test_add_func("/fwupd/firmware/dfu", fu_firmware_dfu_func);
	g_test_add_func("/fwupd/firmware/dfu-patch", fu_firmware_dfu_patch_func);
	g_test_add_func("/fwupd/firmware/dfuse", fu_firmware_dfuse_func);
	g_test_add_func("/fwupd/firmware/fmap", fu_firmware_fmap_func);
	g_test_add_func("/fwupd/firmware/gtypes", fu_firmware_new_from_gtypes_func);
	g_test_add_func("/fwupd/firmware/sorted", fu_firmware_sorted_func);
	return g_test_run();
}
