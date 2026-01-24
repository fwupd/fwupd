/*
 * Copyright 2017 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include <fwupdplugin.h>

#include "fu-cabinet.h"

static GBytes *
fu_test_build_cab(gboolean compressed, ...)
{
	gboolean ret;
	va_list args;
	g_autoptr(FuCabFirmware) cabinet = fu_cab_firmware_new();
	g_autoptr(GError) error = NULL;
	g_autoptr(GBytes) cabinet_blob = NULL;

	fu_cab_firmware_set_compressed(cabinet, compressed);

	/* add each file */
	va_start(args, compressed);
	do {
		const gchar *fn;
		const gchar *text;
		g_autoptr(FuCabImage) img = fu_cab_image_new();
		g_autoptr(GBytes) blob = NULL;

		/* get filename */
		fn = va_arg(args, const gchar *);
		if (fn == NULL)
			break;

		/* get contents */
		text = va_arg(args, const gchar *);
		if (text == NULL)
			break;
		g_debug("creating %s with %s", fn, text);

		/* add a GCabFile to the cabinet */
		blob = g_bytes_new_static(text, strlen(text));
		fu_firmware_set_id(FU_FIRMWARE(img), fn);
		fu_firmware_set_bytes(FU_FIRMWARE(img), blob);
		ret = fu_firmware_add_image(FU_FIRMWARE(cabinet), FU_FIRMWARE(img), &error);
		g_assert_no_error(error);
		g_assert_true(ret);
	} while (TRUE);
	va_end(args);

	/* write the archive to a blob */
	cabinet_blob = fu_firmware_write(FU_FIRMWARE(cabinet), &error);
	g_assert_no_error(error);
	g_assert_nonnull(cabinet_blob);
	return g_steal_pointer(&cabinet_blob);
}

static void
fu_cabinet_func(void)
{
	gboolean ret;
	g_autofree gchar *filename = NULL;
	g_autoptr(FuCabinet) cabinet = fu_cabinet_new();
	g_autoptr(FuFirmware) img1 = NULL;
	g_autoptr(FuFirmware) img2 = NULL;
	g_autoptr(GBytes) blob = NULL;
	g_autoptr(GBytes) jcat_blob1 = g_bytes_new_static("hello", 6);
	g_autoptr(GBytes) jcat_blob2 = g_bytes_new_static("hellX", 6);
	g_autoptr(GError) error = NULL;
	g_autoptr(GInputStream) stream = NULL;

	filename = g_test_build_filename(G_TEST_BUILT,
					 "tests",
					 "multiple-rels",
					 "multiple-rels-1.2.4.cab",
					 NULL);
	stream = fu_input_stream_from_path(filename, &error);
	g_assert_no_error(error);
	g_assert_nonnull(stream);
	ret = fu_firmware_parse_stream(FU_FIRMWARE(cabinet),
				       stream,
				       0x0,
				       FU_FIRMWARE_PARSE_FLAG_CACHE_STREAM,
				       &error);
	g_assert_no_error(error);
	g_assert_true(ret);

	/* add */
	ret = fu_cabinet_add_file(cabinet, "firmware.jcat", jcat_blob1, &error);
	g_assert_no_error(error);
	g_assert_true(ret);

	/* replace */
	ret = fu_cabinet_add_file(cabinet, "firmware.jcat", jcat_blob2, &error);
	g_assert_no_error(error);
	g_assert_true(ret);

	/* get data */
	img1 = fu_firmware_get_image_by_id(FU_FIRMWARE(cabinet), "firmware.jcat", &error);
	g_assert_no_error(error);
	g_assert_nonnull(img1);
	blob = fu_firmware_get_bytes(FU_FIRMWARE(img1), &error);
	g_assert_no_error(error);
	g_assert_nonnull(blob);
	g_assert_cmpstr(g_bytes_get_data(blob, NULL), ==, "hellX");

	/* get data that does not exist */
	img2 = fu_firmware_get_image_by_id(FU_FIRMWARE(cabinet), "foo.jcat", &error);
	g_assert_error(error, FWUPD_ERROR, FWUPD_ERROR_NOT_FOUND);
	g_assert_null(img2);
}

static void
fu_cabinet_success_func(void)
{
	gboolean ret;
	GBytes *blob_tmp;
	g_autoptr(FuCabinet) cabinet = fu_cabinet_new();
	g_autoptr(GBytes) blob = NULL;
	g_autoptr(GError) error = NULL;
	g_autoptr(XbNode) component = NULL;
	g_autoptr(XbNode) csum = NULL;
	g_autoptr(XbNode) rel = NULL;
	g_autoptr(XbNode) req = NULL;
	g_autoptr(XbQuery) query = NULL;

	/* create silo */
	blob = fu_test_build_cab(
	    FALSE,
	    "acme.metainfo.xml",
	    "<component type=\"firmware\">\n"
	    "  <id>com.acme.example.firmware</id>\n"
	    "  <name>ACME Firmware</name>\n"
	    "  <provides>\n"
	    "    <firmware type=\"flashed\">ae56e3fb-6528-5bc4-8b03-012f124075d7</firmware>\n"
	    "  </provides>\n"
	    "  <releases>\n"
	    "    <release version=\"1.2.3\" date=\"2017-09-06\">\n"
	    "      <size type=\"installed\">5</size>\n"
	    "      <checksum filename=\"firmware.dfu\" target=\"content\" "
	    "type=\"sha1\">7c211433f02071597741e6ff5a8ea34789abbf43</checksum>\n"
	    "      <description><p>We fixed things</p></description>\n"
	    "    </release>\n"
	    "  </releases>\n"
	    "  <requires>\n"
	    "    <id compare=\"ge\" version=\"1.0.1\">org.freedesktop.fwupd</id>\n"
	    "  </requires>\n"
	    "</component>",
	    "firmware.dfu",
	    "world",
	    "firmware.dfu.asc",
	    "signature",
	    NULL);
	ret = fu_firmware_parse_bytes(FU_FIRMWARE(cabinet),
				      blob,
				      0x0,
				      FU_FIRMWARE_PARSE_FLAG_CACHE_BLOB,
				      &error);
	g_assert_no_error(error);
	g_assert_true(ret);

	/* verify */
	component = fu_cabinet_get_component(cabinet, "com.acme.example.firmware", &error);
	g_assert_no_error(error);
	g_assert_nonnull(component);
	query = xb_query_new_full(xb_node_get_silo(component),
				  "releases/release",
				  XB_QUERY_FLAG_FORCE_NODE_CACHE,
				  &error);
	g_assert_no_error(error);
	g_assert_nonnull(query);
	rel = xb_node_query_first_full(component, query, &error);
	g_assert_no_error(error);
	g_assert_nonnull(rel);
	g_assert_cmpstr(xb_node_get_attr(rel, "version"), ==, "1.2.3");
	csum = xb_node_query_first(rel, "checksum[@target='content']", &error);
	g_assert_nonnull(csum);
	g_assert_cmpstr(xb_node_get_text(csum), ==, "7c211433f02071597741e6ff5a8ea34789abbf43");
	blob_tmp = xb_node_get_data(rel, "fwupd::FirmwareBasename");
	g_assert_nonnull(blob_tmp);
	req = xb_node_query_first(component, "requires/id", &error);
	g_assert_no_error(error);
	g_assert_nonnull(req);
}

static void
fu_cabinet_artifact_func(void)
{
	gboolean ret;
	g_autoptr(GBytes) blob1 = NULL;
	g_autoptr(GBytes) blob2 = NULL;
	g_autoptr(GBytes) blob3 = NULL;
	g_autoptr(GBytes) blob4 = NULL;
	g_autoptr(GError) error = NULL;
	g_autoptr(FuCabinet) cabinet1 = fu_cabinet_new();
	g_autoptr(FuCabinet) cabinet2 = fu_cabinet_new();
	g_autoptr(FuCabinet) cabinet3 = fu_cabinet_new();
	g_autoptr(FuCabinet) cabinet4 = fu_cabinet_new();

	/* create silo (sha256, using artifacts object) */
	blob1 = fu_test_build_cab(
	    FALSE,
	    "acme.metainfo.xml",
	    "<component type=\"firmware\">\n"
	    "  <id>com.acme.example.firmware</id>\n"
	    "  <provides>\n"
	    "    <firmware type=\"flashed\">b585990a-003e-5270-89d5-3705a17f9a43</firmware>\n"
	    "  </provides>\n"
	    "  <releases>\n"
	    "    <release version=\"1.2.3\" date=\"2017-09-06\">\n"
	    "      <artifacts>\n"
	    "        <artifact type=\"source\">\n"
	    "          <filename>firmware.dfu</filename>\n"
	    "          <checksum "
	    "type=\"sha256\">486EA46224D1BB4FB680F34F7C9AD96A8F24EC88BE73EA8E5A6C65260E9CB8A7</"
	    "checksum>\n"
	    "        </artifact>\n"
	    "      </artifacts>\n"
	    "    </release>\n"
	    "  </releases>\n"
	    "</component>",
	    "firmware.dfu",
	    "world",
	    "firmware.dfu.asc",
	    "signature",
	    NULL);
	ret = fu_firmware_parse_bytes(FU_FIRMWARE(cabinet1),
				      blob1,
				      0x0,
				      FU_FIRMWARE_PARSE_FLAG_CACHE_BLOB,
				      &error);
	g_assert_no_error(error);
	g_assert_true(ret);

	/* create silo (sha1, using artifacts object; mixed case) */
	blob2 = fu_test_build_cab(
	    FALSE,
	    "acme.metainfo.xml",
	    "<component type=\"firmware\">\n"
	    "  <id>com.acme.example.firmware</id>\n"
	    "  <provides>\n"
	    "    <firmware type=\"flashed\">b585990a-003e-5270-89d5-3705a17f9a43</firmware>\n"
	    "  </provides>\n"
	    "  <releases>\n"
	    "    <release version=\"1.2.3\" date=\"2017-09-06\">\n"
	    "      <artifacts>\n"
	    "        <artifact type=\"source\">\n"
	    "          <filename>firmware.dfu</filename>\n"
	    "          <checksum "
	    "type=\"sha1\">7c211433f02071597741e6ff5a8ea34789abbF43</"
	    "checksum>\n"
	    "        </artifact>\n"
	    "      </artifacts>\n"
	    "    </release>\n"
	    "  </releases>\n"
	    "</component>",
	    "firmware.dfu",
	    "world",
	    "firmware.dfu.asc",
	    "signature",
	    NULL);
	ret = fu_firmware_parse_bytes(FU_FIRMWARE(cabinet2),
				      blob2,
				      0x0,
				      FU_FIRMWARE_PARSE_FLAG_CACHE_BLOB,
				      &error);
	g_assert_no_error(error);
	g_assert_true(ret);

	/* create silo (sha512, using artifacts object; lower case) */
	blob3 = fu_test_build_cab(
	    FALSE,
	    "acme.metainfo.xml",
	    "<component type=\"firmware\">\n"
	    "  <id>com.acme.example.firmware</id>\n"
	    "  <provides>\n"
	    "    <firmware type=\"flashed\">b585990a-003e-5270-89d5-3705a17f9a43</firmware>\n"
	    "  </provides>\n"
	    "  <releases>\n"
	    "    <release version=\"1.2.3\" date=\"2017-09-06\">\n"
	    "      <artifacts>\n"
	    "        <artifact type=\"source\">\n"
	    "          <filename>firmware.dfu</filename>\n"
	    "          <checksum "
	    "type=\"sha512\">"
	    "11853df40f4b2b919d3815f64792e58d08663767a494bcbb38c0b2389d9140bbb170281b"
	    "4a847be7757bde12c9cd0054ce3652d0ad3a1a0c92babb69798246ee</"
	    "checksum>\n"
	    "        </artifact>\n"
	    "      </artifacts>\n"
	    "    </release>\n"
	    "  </releases>\n"
	    "</component>",
	    "firmware.dfu",
	    "world",
	    "firmware.dfu.asc",
	    "signature",
	    NULL);
	ret = fu_firmware_parse_bytes(FU_FIRMWARE(cabinet3),
				      blob3,
				      0x0,
				      FU_FIRMWARE_PARSE_FLAG_CACHE_BLOB,
				      &error);
	g_assert_no_error(error);
	g_assert_true(ret);

	/* create silo (legacy release object) */
	blob4 = fu_test_build_cab(
	    FALSE,
	    "acme.metainfo.xml",
	    "<component type=\"firmware\">\n"
	    "  <id>com.acme.example.firmware</id>\n"
	    "  <provides>\n"
	    "    <firmware type=\"flashed\">b585990a-003e-5270-89d5-3705a17f9a43</firmware>\n"
	    "  </provides>\n"
	    "  <releases>\n"
	    "    <release version=\"1.2.3\" date=\"2017-09-06\">\n"
	    "        <checksum "
	    "target=\"content\" "
	    "filename=\"firmware.dfu\">"
	    "486EA46224D1BB4FB680F34F7C9AD96A8F24EC88BE73EA8E5A6C65260E9CB8A7</"
	    "checksum>\n"
	    "    </release>\n"
	    "  </releases>\n"
	    "</component>",
	    "firmware.dfu",
	    "world",
	    "firmware.dfu.asc",
	    "signature",
	    NULL);
	ret = fu_firmware_parse_bytes(FU_FIRMWARE(cabinet4),
				      blob4,
				      0x0,
				      FU_FIRMWARE_PARSE_FLAG_CACHE_BLOB,
				      &error);
	g_assert_no_error(error);
	g_assert_true(ret);
}

static void
fu_cabinet_unsigned_func(void)
{
	GBytes *blob_tmp;
	gboolean ret;
	g_autoptr(FuCabinet) cabinet = fu_cabinet_new();
	g_autoptr(GBytes) blob = NULL;
	g_autoptr(GError) error = NULL;
	g_autoptr(XbNode) component = NULL;
	g_autoptr(XbNode) csum = NULL;
	g_autoptr(XbNode) rel = NULL;
	g_autoptr(XbQuery) query = NULL;

	/* create silo */
	blob = fu_test_build_cab(
	    FALSE,
	    "acme.metainfo.xml",
	    "<component type=\"firmware\">\n"
	    "  <id>com.acme.example.firmware</id>\n"
	    "  <provides>\n"
	    "    <firmware type=\"flashed\">b585990a-003e-5270-89d5-3705a17f9a43</firmware>\n"
	    "  </provides>\n"
	    "  <releases>\n"
	    "    <release version=\"1.2.3\"/>\n"
	    "  </releases>\n"
	    "</component>",
	    "firmware.bin",
	    "world",
	    NULL);
	ret = fu_firmware_parse_bytes(FU_FIRMWARE(cabinet),
				      blob,
				      0x0,
				      FU_FIRMWARE_PARSE_FLAG_CACHE_BLOB,
				      &error);
	g_assert_no_error(error);
	g_assert_true(ret);

	/* verify */
	component = fu_cabinet_get_component(cabinet, "com.acme.example.firmware", &error);
	g_assert_no_error(error);
	g_assert_nonnull(component);
	query = xb_query_new_full(xb_node_get_silo(component),
				  "releases/release",
				  XB_QUERY_FLAG_FORCE_NODE_CACHE,
				  &error);
	g_assert_no_error(error);
	g_assert_nonnull(query);
	rel = xb_node_query_first_full(component, query, &error);
	g_assert_no_error(error);
	g_assert_nonnull(rel);
	g_assert_cmpstr(xb_node_get_attr(rel, "version"), ==, "1.2.3");
	csum = xb_node_query_first(rel, "checksum[@target='content']", &error);
	g_assert_null(csum);
	blob_tmp = xb_node_get_data(rel, "fwupd::FirmwareBasename");
	g_assert_nonnull(blob_tmp);
}

static void
fu_cabinet_sha256_func(void)
{
	gboolean ret;
	g_autoptr(FuCabinet) cabinet = fu_cabinet_new();
	g_autoptr(GBytes) blob = NULL;
	g_autoptr(GError) error = NULL;

	/* create silo */
	blob = fu_test_build_cab(
	    FALSE,
	    "acme.metainfo.xml",
	    "<component type=\"firmware\">\n"
	    "  <id>com.acme.example.firmware</id>\n"
	    "  <provides>\n"
	    "    <firmware type=\"flashed\">b585990a-003e-5270-89d5-3705a17f9a43</firmware>\n"
	    "  </provides>\n"
	    "  <releases>\n"
	    "    <release version=\"1.2.3\" date=\"2017-09-06\">\n"
	    "      <checksum target=\"content\" "
	    "type=\"sha256\">486ea46224d1bb4fb680f34f7c9ad96a8f24ec88be73ea8e5a6c65260e9cb8a7</"
	    "checksum>\n"
	    "    </release>\n"
	    "  </releases>\n"
	    "</component>",
	    "firmware.bin",
	    "world",
	    NULL);
	ret = fu_firmware_parse_bytes(FU_FIRMWARE(cabinet),
				      blob,
				      0x0,
				      FU_FIRMWARE_PARSE_FLAG_CACHE_BLOB,
				      &error);
	g_assert_no_error(error);
	g_assert_true(ret);
}

static void
fu_cabinet_folder_func(void)
{
	GBytes *blob_tmp;
	gboolean ret;
	g_autoptr(FuCabinet) cabinet = fu_cabinet_new();
	g_autoptr(GBytes) blob = NULL;
	g_autoptr(GError) error = NULL;
	g_autoptr(XbNode) component = NULL;
	g_autoptr(XbNode) rel = NULL;
	g_autoptr(XbQuery) query = NULL;

	/* create silo */
	blob = fu_test_build_cab(
	    FALSE,
	    "lvfs\\acme.metainfo.xml",
	    "<component type=\"firmware\">\n"
	    "  <id>com.acme.example.firmware</id>\n"
	    "  <provides>\n"
	    "    <firmware type=\"flashed\">b585990a-003e-5270-89d5-3705a17f9a43</firmware>\n"
	    "  </provides>\n"
	    "  <releases>\n"
	    "    <release version=\"1.2.3\"/>\n"
	    "  </releases>\n"
	    "</component>",
	    "lvfs\\firmware.bin",
	    "world",
	    NULL);
	ret = fu_firmware_parse_bytes(FU_FIRMWARE(cabinet),
				      blob,
				      0x0,
				      FU_FIRMWARE_PARSE_FLAG_CACHE_BLOB,
				      &error);
	g_assert_no_error(error);
	g_assert_true(ret);

	/* verify */
	component = fu_cabinet_get_component(cabinet, "com.acme.example.firmware", &error);
	g_assert_no_error(error);
	g_assert_nonnull(component);
	query = xb_query_new_full(xb_node_get_silo(component),
				  "releases/release",
				  XB_QUERY_FLAG_FORCE_NODE_CACHE,
				  &error);
	g_assert_no_error(error);
	g_assert_nonnull(query);
	rel = xb_node_query_first_full(component, query, &error);
	g_assert_no_error(error);
	g_assert_nonnull(rel);
	g_assert_cmpstr(xb_node_get_attr(rel, "version"), ==, "1.2.3");
	blob_tmp = xb_node_get_data(rel, "fwupd::FirmwareBasename");
	g_assert_nonnull(blob_tmp);
}

static void
fu_cabinet_error_no_metadata_func(void)
{
	gboolean ret;
	g_autoptr(FuCabinet) cabinet = fu_cabinet_new();
	g_autoptr(GBytes) blob = NULL;
	g_autoptr(GError) error = NULL;

	blob = fu_test_build_cab(FALSE, "foo.txt", "hello", "bar.txt", "world", NULL);
	ret = fu_firmware_parse_bytes(FU_FIRMWARE(cabinet),
				      blob,
				      0x0,
				      FU_FIRMWARE_PARSE_FLAG_NONE,
				      &error);
	g_assert_error(error, FWUPD_ERROR, FWUPD_ERROR_INVALID_FILE);
	g_assert_false(ret);
}

static void
fu_cabinet_error_wrong_size_func(void)
{
	gboolean ret;
	g_autoptr(FuCabinet) cabinet = fu_cabinet_new();
	g_autoptr(GBytes) blob = NULL;
	g_autoptr(GError) error = NULL;

	blob = fu_test_build_cab(
	    FALSE,
	    "acme.metainfo.xml",
	    "<component type=\"firmware\">\n"
	    "  <id>com.acme.example.firmware</id>\n"
	    "  <provides>\n"
	    "    <firmware type=\"flashed\">b585990a-003e-5270-89d5-3705a17f9a43</firmware>\n"
	    "  </provides>\n"
	    "  <releases>\n"
	    "    <release version=\"1.2.3\">\n"
	    "      <size type=\"installed\">7004701</size>\n"
	    "      <checksum filename=\"firmware.bin\" target=\"content\" "
	    "type=\"sha1\">deadbeef</checksum>\n"
	    "    </release>\n"
	    "  </releases>\n"
	    "</component>",
	    "firmware.bin",
	    "world",
	    NULL);
	ret = fu_firmware_parse_bytes(FU_FIRMWARE(cabinet),
				      blob,
				      0x0,
				      FU_FIRMWARE_PARSE_FLAG_NONE,
				      &error);
	g_assert_error(error, FWUPD_ERROR, FWUPD_ERROR_INVALID_FILE);
	g_assert_false(ret);
}

static void
fu_cabinet_error_missing_file_func(void)
{
	gboolean ret;
	g_autoptr(FuCabinet) cabinet = fu_cabinet_new();
	g_autoptr(GBytes) blob = NULL;
	g_autoptr(GError) error = NULL;

	blob = fu_test_build_cab(
	    FALSE,
	    "acme.metainfo.xml",
	    "<component type=\"firmware\">\n"
	    "  <id>com.acme.example.firmware</id>\n"
	    "  <provides>\n"
	    "    <firmware type=\"flashed\">b585990a-003e-5270-89d5-3705a17f9a43</firmware>\n"
	    "  </provides>\n"
	    "  <releases>\n"
	    "    <release version=\"1.2.3\">\n"
	    "      <checksum filename=\"firmware.dfu\" target=\"content\"/>\n"
	    "    </release>\n"
	    "  </releases>\n"
	    "</component>",
	    "firmware.bin",
	    "world",
	    NULL);
	ret = fu_firmware_parse_bytes(FU_FIRMWARE(cabinet),
				      blob,
				      0x0,
				      FU_FIRMWARE_PARSE_FLAG_NONE,
				      &error);
	g_assert_error(error, FWUPD_ERROR, FWUPD_ERROR_INVALID_FILE);
	g_assert_false(ret);
}

static void
fu_cabinet_error_size_func(void)
{
	gboolean ret;
	g_autoptr(FuCabinet) cabinet = fu_cabinet_new();
	g_autoptr(GBytes) blob = NULL;
	g_autoptr(GError) error = NULL;

	blob = fu_test_build_cab(
	    FALSE,
	    "acme.metainfo.xml",
	    "<component type=\"firmware\">\n"
	    "  <id>com.acme.example.firmware</id>\n"
	    "  <provides>\n"
	    "    <firmware type=\"flashed\">b585990a-003e-5270-89d5-3705a17f9a43</firmware>\n"
	    "  </provides>\n"
	    "  <releases>\n"
	    "    <release version=\"1.2.3\"/>\n"
	    "  </releases>\n"
	    "</component>",
	    "firmware.bin",
	    "world",
	    NULL);
	fu_firmware_set_size_max(FU_FIRMWARE(cabinet), 123);
	ret = fu_firmware_parse_bytes(FU_FIRMWARE(cabinet),
				      blob,
				      0x0,
				      FU_FIRMWARE_PARSE_FLAG_NONE,
				      &error);
	g_assert_error(error, FWUPD_ERROR, FWUPD_ERROR_INVALID_FILE);
	g_assert_false(ret);
}

static void
fu_cabinet_error_wrong_checksum_func(void)
{
	gboolean ret;
	g_autoptr(FuCabinet) cabinet = fu_cabinet_new();
	g_autoptr(GBytes) blob = NULL;
	g_autoptr(GError) error = NULL;

	blob = fu_test_build_cab(
	    FALSE,
	    "acme.metainfo.xml",
	    "<component type=\"firmware\">\n"
	    "  <id>com.acme.example.firmware</id>\n"
	    "  <provides>\n"
	    "    <firmware type=\"flashed\">b585990a-003e-5270-89d5-3705a17f9a43</firmware>\n"
	    "  </provides>\n"
	    "  <releases>\n"
	    "    <release version=\"1.2.3\">\n"
	    "      <checksum filename=\"firmware.bin\" target=\"content\" "
	    "type=\"sha1\">deadbeef</checksum>\n"
	    "    </release>\n"
	    "  </releases>\n"
	    "</component>",
	    "firmware.bin",
	    "world",
	    NULL);
	ret = fu_firmware_parse_bytes(FU_FIRMWARE(cabinet),
				      blob,
				      0x0,
				      FU_FIRMWARE_PARSE_FLAG_NONE,
				      &error);
	g_assert_error(error, FWUPD_ERROR, FWUPD_ERROR_INVALID_FILE);
	g_assert_false(ret);
}

int
main(int argc, char **argv)
{
	g_test_init(&argc, &argv, NULL);
	g_test_add_func("/fwupd/cabinet", fu_cabinet_func);
	g_test_add_func("/fwupd/cabinet/success", fu_cabinet_success_func);
	g_test_add_func("/fwupd/cabinet/success-artifact", fu_cabinet_artifact_func);
	g_test_add_func("/fwupd/cabinet/success-unsigned", fu_cabinet_unsigned_func);
	g_test_add_func("/fwupd/cabinet/success-folder", fu_cabinet_folder_func);
	g_test_add_func("/fwupd/cabinet/success-sha256", fu_cabinet_sha256_func);
	g_test_add_func("/fwupd/cabinet/error-no-metadata", fu_cabinet_error_no_metadata_func);
	g_test_add_func("/fwupd/cabinet/error-wrong-size", fu_cabinet_error_wrong_size_func);
	g_test_add_func("/fwupd/cabinet/error-wrong-checksum",
			fu_cabinet_error_wrong_checksum_func);
	g_test_add_func("/fwupd/cabinet/error-missing-file", fu_cabinet_error_missing_file_func);
	g_test_add_func("/fwupd/cabinet/error-size", fu_cabinet_error_size_func);
	return g_test_run();
}
