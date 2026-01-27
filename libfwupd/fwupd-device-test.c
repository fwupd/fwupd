/*
 * Copyright 2017 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include "fwupd-codec.h"
#include "fwupd-device-private.h"
#include "fwupd-error.h"
#include "fwupd-test.h"

static void
fwupd_device_filter_func(void)
{
	g_autoptr(FwupdDevice) dev = fwupd_device_new();
	fwupd_device_add_flag(dev, FWUPD_DEVICE_FLAG_SIGNED_PAYLOAD);
	fwupd_device_add_flag(dev, FWUPD_DEVICE_FLAG_SUPPORTED);

	/* none */
	g_assert_true(
	    fwupd_device_match_flags(dev, FWUPD_DEVICE_FLAG_NONE, FWUPD_DEVICE_FLAG_NONE));

	/* include */
	g_assert_true(fwupd_device_match_flags(dev,
					       FWUPD_DEVICE_FLAG_SIGNED_PAYLOAD,
					       FWUPD_DEVICE_FLAG_NONE));
	g_assert_true(
	    fwupd_device_match_flags(dev, FWUPD_DEVICE_FLAG_SUPPORTED, FWUPD_DEVICE_FLAG_NONE));
	g_assert_true(
	    fwupd_device_match_flags(dev,
				     FWUPD_DEVICE_FLAG_SIGNED_PAYLOAD | FWUPD_DEVICE_FLAG_SUPPORTED,
				     FWUPD_DEVICE_FLAG_NONE));
	g_assert_false(fwupd_device_match_flags(dev,
						FWUPD_DEVICE_FLAG_ANOTHER_WRITE_REQUIRED,
						FWUPD_DEVICE_FLAG_NONE));

	/* exclude, i.e. ~flag */
	g_assert_false(fwupd_device_match_flags(dev,
						FWUPD_DEVICE_FLAG_NONE,
						FWUPD_DEVICE_FLAG_SIGNED_PAYLOAD));
	g_assert_false(
	    fwupd_device_match_flags(dev, FWUPD_DEVICE_FLAG_NONE, FWUPD_DEVICE_FLAG_SUPPORTED));
	g_assert_false(fwupd_device_match_flags(dev,
						FWUPD_DEVICE_FLAG_NONE,
						FWUPD_DEVICE_FLAG_SIGNED_PAYLOAD |
						    FWUPD_DEVICE_FLAG_SUPPORTED));
	g_assert_true(fwupd_device_match_flags(dev,
					       FWUPD_DEVICE_FLAG_NONE,
					       FWUPD_DEVICE_FLAG_ANOTHER_WRITE_REQUIRED));
}

static void
fwupd_device_func(void)
{
	gboolean ret;
	g_autofree gchar *data = NULL;
	g_autofree gchar *str = NULL;
	g_autoptr(FwupdDevice) dev = NULL;
	g_autoptr(FwupdDevice) dev2 = fwupd_device_new();
	g_autoptr(FwupdDevice) dev_new = fwupd_device_new();
	g_autoptr(FwupdRelease) rel = NULL;
	g_autoptr(GError) error = NULL;
	g_autoptr(GString) str_ascii = NULL;

	/* create dummy object */
	dev = fwupd_device_new();
	fwupd_device_add_checksum(dev, "beefdead");
	fwupd_device_set_created(dev, 1);
	fwupd_device_add_flag(dev, FWUPD_DEVICE_FLAG_UPDATABLE);
	fwupd_device_set_id(dev, "0000000000000000000000000000000000000000");
	fwupd_device_set_modified(dev, 60 * 60 * 24);
	fwupd_device_set_name(dev, "ColorHug2");
	fwupd_device_set_branch(dev, "community");
	fwupd_device_add_guid(dev, "2082b5e0-7a64-478a-b1b2-e3404fab6dad");
	fwupd_device_add_guid(dev, "00000000-0000-0000-0000-000000000000");
	fwupd_device_add_instance_id(dev, "USB\\VID_1234&PID_0001");
	fwupd_device_add_icon(dev, "input-gaming");
	fwupd_device_add_icon(dev, "input-mouse");
	fwupd_device_add_vendor_id(dev, "USB:0x1234");
	fwupd_device_add_vendor_id(dev, "PCI:0x5678");
	fwupd_device_add_flag(dev, FWUPD_DEVICE_FLAG_UPDATABLE | FWUPD_DEVICE_FLAG_REQUIRE_AC);
	g_assert_true(fwupd_device_has_flag(dev, FWUPD_DEVICE_FLAG_REQUIRE_AC));
	g_assert_true(fwupd_device_has_flag(dev, FWUPD_DEVICE_FLAG_UPDATABLE));
	g_assert_false(fwupd_device_has_flag(dev, FWUPD_DEVICE_FLAG_HISTORICAL));
	rel = fwupd_release_new();
	fwupd_release_add_flag(rel, FWUPD_RELEASE_FLAG_TRUSTED_PAYLOAD);
	fwupd_release_add_checksum(rel, "deadbeef");
	fwupd_release_set_description(rel, "<p>Hi there!</p>");
	fwupd_release_set_filename(rel, "firmware.bin");
	fwupd_release_set_appstream_id(rel, "org.dave.ColorHug.firmware");
	fwupd_release_set_size(rel, 1024);
	fwupd_release_add_location(rel, "http://foo.com");
	fwupd_release_add_location(rel, "ftp://foo.com");
	fwupd_release_add_tag(rel, "vendor-2021q1");
	fwupd_release_add_tag(rel, "vendor-2021q2");
	fwupd_release_set_version(rel, "1.2.3");
	fwupd_device_add_release(dev, rel);
	str = fwupd_codec_to_string(FWUPD_CODEC(dev));
	g_debug("%s", str);

	/* check GUIDs */
	g_assert_true(fwupd_device_has_guid(dev, "2082b5e0-7a64-478a-b1b2-e3404fab6dad"));
	g_assert_true(fwupd_device_has_guid(dev, "00000000-0000-0000-0000-000000000000"));
	g_assert_false(fwupd_device_has_guid(dev, "xxxxxxxx-xxxx-xxxx-xxxx-xxxxxxxxxxxx"));

	/* convert the new non-breaking space back into a normal space:
	 * https://gitlab.gnome.org/GNOME/glib/commit/76af5dabb4a25956a6c41a75c0c7feeee74496da */
	str_ascii = g_string_new(str);
	g_string_replace(str_ascii, " ", " ", 0);
	ret = fu_test_compare_lines(
	    str_ascii->str,
	    "FwupdDevice:\n"
	    "  DeviceId:             0000000000000000000000000000000000000000\n"
	    "  Name:                 ColorHug2\n"
	    "  Guid:                 18f514d2-c12e-581f-a696-cc6d6c271699 "
	    "← USB\\VID_1234&PID_0001 ⚠\n"
	    "  Guid:                 2082b5e0-7a64-478a-b1b2-e3404fab6dad\n"
	    "  Guid:                 00000000-0000-0000-0000-000000000000\n"
	    "  Branch:               community\n"
	    "  Flags:                updatable|require-ac\n"
	    "  Checksum:             SHA1(beefdead)\n"
	    "  VendorId:             USB:0x1234\n"
	    "  VendorId:             PCI:0x5678\n"
	    "  Icon:                 input-gaming,input-mouse\n"
	    "  Created:              1970-01-01 00:00:01\n"
	    "  Modified:             1970-01-02 00:00:00\n"
	    "  FwupdRelease:\n"
	    "    AppstreamId:        org.dave.ColorHug.firmware\n"
	    "    Description:        <p>Hi there!</p>\n"
	    "    Version:            1.2.3\n"
	    "    Filename:           firmware.bin\n"
	    "    Checksum:           SHA1(deadbeef)\n"
	    "    Tags:               vendor-2021q1\n"
	    "    Tags:               vendor-2021q2\n"
	    "    Size:               1.0 kB\n"
	    "    Uri:                http://foo.com\n"
	    "    Uri:                ftp://foo.com\n"
	    "    Flags:              trusted-payload\n",
	    &error);
	g_assert_no_error(error);
	g_assert_true(ret);

	/* export to json */
	data = fwupd_codec_to_json_string(FWUPD_CODEC(dev), FWUPD_CODEC_FLAG_TRUSTED, &error);
	g_assert_no_error(error);
	g_assert_nonnull(data);
	ret =
	    fu_test_compare_lines(data,
				  "{\n"
				  "  \"Name\": \"ColorHug2\",\n"
				  "  \"DeviceId\": \"0000000000000000000000000000000000000000\",\n"
				  "  \"InstanceIds\": [\n"
				  "    \"USB\\\\VID_1234&PID_0001\"\n"
				  "  ],\n"
				  "  \"Guid\": [\n"
				  "    \"2082b5e0-7a64-478a-b1b2-e3404fab6dad\",\n"
				  "    \"00000000-0000-0000-0000-000000000000\"\n"
				  "  ],\n"
				  "  \"Branch\": \"community\",\n"
				  "  \"Flags\": [\n"
				  "    \"updatable\",\n"
				  "    \"require-ac\"\n"
				  "  ],\n"
				  "  \"Checksums\": [\n"
				  "    \"beefdead\"\n"
				  "  ],\n"
				  "  \"VendorIds\": [\n"
				  "    \"USB:0x1234\",\n"
				  "    \"PCI:0x5678\"\n"
				  "  ],\n"
				  "  \"Icons\": [\n"
				  "    \"input-gaming\",\n"
				  "    \"input-mouse\"\n"
				  "  ],\n"
				  "  \"Created\": 1,\n"
				  "  \"Modified\": 86400,\n"
				  "  \"Releases\": [\n"
				  "    {\n"
				  "      \"AppstreamId\": \"org.dave.ColorHug.firmware\",\n"
				  "      \"Description\": \"<p>Hi there!</p>\",\n"
				  "      \"Version\": \"1.2.3\",\n"
				  "      \"Filename\": \"firmware.bin\",\n"
				  "      \"Checksum\": [\n"
				  "        \"deadbeef\"\n"
				  "      ],\n"
				  "      \"Tags\": [\n"
				  "        \"vendor-2021q1\",\n"
				  "        \"vendor-2021q2\"\n"
				  "      ],\n"
				  "      \"Size\": 1024,\n"
				  "      \"Locations\": [\n"
				  "        \"http://foo.com\",\n"
				  "        \"ftp://foo.com\"\n"
				  "      ],\n"
				  "      \"Flags\": [\n"
				  "        \"trusted-payload\"\n"
				  "      ]\n"
				  "    }\n"
				  "  ]\n"
				  "}",
				  &error);
	g_assert_no_error(error);
	g_assert_true(ret);

	/* incorporate */
	fwupd_device_incorporate(dev_new, dev);
	g_assert_true(fwupd_device_has_vendor_id(dev_new, "USB:0x1234"));
	g_assert_true(fwupd_device_has_vendor_id(dev_new, "PCI:0x5678"));
	g_assert_true(fwupd_device_has_instance_id(dev_new, "USB\\VID_1234&PID_0001"));

	/* from JSON */
	ret = fwupd_codec_from_json_string(FWUPD_CODEC(dev2), data, &error);
	if (g_error_matches(error, FWUPD_ERROR, FWUPD_ERROR_NOT_SUPPORTED)) {
		g_test_skip(error->message);
		return;
	}
	g_assert_no_error(error);
	g_assert_true(ret);
	g_assert_true(fwupd_device_has_vendor_id(dev2, "USB:0x1234"));
	g_assert_true(fwupd_device_has_instance_id(dev2, "USB\\VID_1234&PID_0001"));
	g_assert_true(fwupd_device_has_flag(dev2, FWUPD_DEVICE_FLAG_UPDATABLE));
	g_assert_false(fwupd_device_has_flag(dev2, FWUPD_DEVICE_FLAG_LOCKED));
}

int
main(int argc, char **argv)
{
	g_test_init(&argc, &argv, NULL);
	g_test_add_func("/fwupd/device", fwupd_device_func);
	g_test_add_func("/fwupd/device/filter", fwupd_device_filter_func);
	return g_test_run();
}
