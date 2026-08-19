/*
 * Copyright 2017 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include "fwupd-remote-private.h"

#include "fu-binder-common.h"
#include "fu-test.h"

static void
fu_binder_remote_func(void)
{
	aidl_fwupd::FwupdRemote r;
	g_autoptr(FwupdRemote) remote = fwupd_remote_new();
	g_autoptr(FwupdRemote) remote2 = NULL;

	fwupd_remote_set_id(remote, "id");
	fwupd_remote_set_kind(remote, FWUPD_REMOTE_KIND_DOWNLOAD);
	fwupd_remote_set_report_uri(remote, "https://fwupd.org/report");
	fwupd_remote_set_metadata_uri(remote, "https://fwupd.org/metadata.xml.gz");
	fwupd_remote_set_firmware_base_uri(remote, "https://fwupd.org/downloads");
	fwupd_remote_set_username(remote, "user");
	fwupd_remote_set_title(remote, "title");
	fwupd_remote_set_privacy_uri(remote, "https://fwupd.org/privacy");
	fwupd_remote_set_agreement(remote, "agreement");
	fwupd_remote_set_filename_cache(remote, "/tmp/cache.xml.gz");
	fwupd_remote_set_filename_source(remote, "/etc/fwupd/remotes.d/lvfs.conf");
	fwupd_remote_set_remotes_dir(remote, "/etc/fwupd/remotes.d");
	fwupd_remote_add_flag(remote, FWUPD_REMOTE_FLAG_ENABLED);
	fwupd_remote_add_flag(remote, FWUPD_REMOTE_FLAG_APPROVAL_REQUIRED);
	fwupd_remote_set_priority(remote, 5);
	fwupd_remote_set_mtime(remote, 0x1234);
	fwupd_remote_set_refresh_interval(remote, 86400);
	fwupd_remote_set_order_after(remote, "afterme");
	fwupd_remote_set_order_before(remote, "beforeme");

	r = fu_binder_remote_to_aidl(remote);
	remote2 = fu_binder_remote_from_aidl(r);

	g_assert_cmpstr(fwupd_remote_get_id(remote2), ==, "id");
	g_assert_cmpint(fwupd_remote_get_kind(remote2), ==, FWUPD_REMOTE_KIND_DOWNLOAD);
	g_assert_cmpstr(fwupd_remote_get_report_uri(remote2), ==, "https://fwupd.org/report");
	g_assert_cmpstr(fwupd_remote_get_metadata_uri(remote2),
			==,
			"https://fwupd.org/metadata.xml.gz");
	g_assert_cmpstr(fwupd_remote_get_firmware_base_uri(remote2),
			==,
			"https://fwupd.org/downloads");
	g_assert_cmpstr(fwupd_remote_get_username(remote2), ==, "user");
	g_assert_cmpstr(fwupd_remote_get_title(remote2), ==, "title");
	g_assert_cmpstr(fwupd_remote_get_privacy_uri(remote2), ==, "https://fwupd.org/privacy");
	g_assert_cmpstr(fwupd_remote_get_agreement(remote2), ==, "agreement");
	g_assert_cmpstr(fwupd_remote_get_filename_cache(remote2), ==, "/tmp/cache.xml.gz");
	g_assert_cmpstr(fwupd_remote_get_filename_source(remote2),
			==,
			"/etc/fwupd/remotes.d/lvfs.conf");
	g_assert_cmpstr(fwupd_remote_get_remotes_dir(remote2), ==, "/etc/fwupd/remotes.d");
	g_assert_true(fwupd_remote_has_flag(remote2, FWUPD_REMOTE_FLAG_ENABLED));
	g_assert_true(fwupd_remote_has_flag(remote2, FWUPD_REMOTE_FLAG_APPROVAL_REQUIRED));
	g_assert_cmpint(fwupd_remote_get_priority(remote2), ==, 5);
	g_assert_cmpint(fwupd_remote_get_mtime(remote2), ==, 0x1234);
	g_assert_cmpint(fwupd_remote_get_refresh_interval(remote2), ==, 86400);
	g_assert_cmpstr(fwupd_remote_get_order_after(remote2)[0], ==, "afterme");
	g_assert_cmpstr(fwupd_remote_get_order_before(remote2)[0], ==, "beforeme");

	/* fields the daemon exports but that cannot be set on the device again */
	g_assert_true(r.approvalRequired);
}

static void
fu_binder_release_func(void)
{
	aidl_fwupd::FwupdRelease r;
	g_autoptr(FwupdRelease) release = fwupd_release_new();
	g_autoptr(FwupdRelease) release2 = NULL;

	fwupd_release_set_id(release, "id");
	fwupd_release_set_remote_id(release, "lvfs");
	fwupd_release_set_name(release, "name");
	fwupd_release_set_version(release, "1.2.3");
	fwupd_release_set_filename(release, "firmware.cab");
	fwupd_release_set_appstream_id(release, "com.hughski.ColorHug.firmware");
	fwupd_release_set_name_variant_suffix(release, "suffix");
	fwupd_release_set_summary(release, "summary");
	fwupd_release_set_description(release, "description");
	fwupd_release_set_branch(release, "stable");
	fwupd_release_set_protocol(release, "com.hughski.colorhug");
	fwupd_release_add_category(release, "X-Device");
	fwupd_release_add_issue(release, "CVE-2023-1234");
	fwupd_release_add_checksum(release, "deadbeef");
	fwupd_release_add_tag(release, "vendor-2021q1");
	fwupd_release_set_license(release, "LGPL-2.1-or-later");
	fwupd_release_add_location(release, "http://foo");
	fwupd_release_set_homepage(release, "https://hughski.com");
	fwupd_release_set_details_url(release, "https://hughski.com/details");
	fwupd_release_set_source_url(release, "https://hughski.com/source");
	fwupd_release_set_sbom_url(release, "https://hughski.com/sbom");
	fwupd_release_set_vendor(release, "Hughski Limited");
	fwupd_release_set_detach_caption(release, "detach");
	fwupd_release_set_detach_image(release, "detach.png");
	fwupd_release_set_update_message(release, "message");
	fwupd_release_set_update_image(release, "update.png");
	fwupd_release_set_size(release, 12345);
	fwupd_release_set_created(release, 0x5678);
	fwupd_release_add_flag(release, FWUPD_RELEASE_FLAG_IS_UPGRADE);
	fwupd_release_set_urgency(release, FWUPD_RELEASE_URGENCY_HIGH);
	fwupd_release_set_install_duration(release, 42);

	r = fu_binder_release_to_aidl(release);
	release2 = fu_binder_release_from_aidl(r);

	g_assert_cmpstr(fwupd_release_get_id(release2), ==, "id");
	g_assert_cmpstr(fwupd_release_get_remote_id(release2), ==, "lvfs");
	g_assert_cmpstr(fwupd_release_get_name(release2), ==, "name");
	g_assert_cmpstr(fwupd_release_get_version(release2), ==, "1.2.3");
	g_assert_cmpstr(fwupd_release_get_filename(release2), ==, "firmware.cab");
	g_assert_cmpstr(fwupd_release_get_appstream_id(release2),
			==,
			"com.hughski.ColorHug.firmware");
	g_assert_cmpstr(fwupd_release_get_name_variant_suffix(release2), ==, "suffix");
	g_assert_cmpstr(fwupd_release_get_summary(release2), ==, "summary");
	g_assert_cmpstr(fwupd_release_get_description(release2), ==, "description");
	g_assert_cmpstr(fwupd_release_get_branch(release2), ==, "stable");
	g_assert_cmpstr(fwupd_release_get_protocol(release2), ==, "com.hughski.colorhug");
	g_assert_cmpint(fwupd_release_get_categories(release2)->len, ==, 1);
	g_assert_cmpint(fwupd_release_get_issues(release2)->len, ==, 1);
	g_assert_cmpint(fwupd_release_get_checksums(release2)->len, ==, 1);
	g_assert_cmpint(fwupd_release_get_tags(release2)->len, ==, 1);
	g_assert_cmpstr(fwupd_release_get_license(release2), ==, "LGPL-2.1-or-later");
	g_assert_cmpint(fwupd_release_get_locations(release2)->len, ==, 1);
	g_assert_cmpstr(fwupd_release_get_homepage(release2), ==, "https://hughski.com");
	g_assert_cmpstr(fwupd_release_get_details_url(release2), ==, "https://hughski.com/details");
	g_assert_cmpstr(fwupd_release_get_source_url(release2), ==, "https://hughski.com/source");
	g_assert_cmpstr(fwupd_release_get_sbom_url(release2), ==, "https://hughski.com/sbom");
	g_assert_cmpstr(fwupd_release_get_vendor(release2), ==, "Hughski Limited");
	g_assert_cmpstr(fwupd_release_get_detach_caption(release2), ==, "detach");
	g_assert_cmpstr(fwupd_release_get_detach_image(release2), ==, "detach.png");
	g_assert_cmpstr(fwupd_release_get_update_message(release2), ==, "message");
	g_assert_cmpstr(fwupd_release_get_update_image(release2), ==, "update.png");
	g_assert_cmpint(fwupd_release_get_size(release2), ==, 12345);
	g_assert_cmpint(fwupd_release_get_created(release2), ==, 0x5678);
	g_assert_true(fwupd_release_has_flag(release2, FWUPD_RELEASE_FLAG_IS_UPGRADE));
	g_assert_cmpint(fwupd_release_get_urgency(release2), ==, FWUPD_RELEASE_URGENCY_HIGH);
	g_assert_cmpint(fwupd_release_get_install_duration(release2), ==, 42);
}

static void
fu_binder_device_func(void)
{
	aidl_fwupd::FwupdDevice r;
	g_autoptr(FwupdDevice) device = fwupd_device_new();
	g_autoptr(FwupdDevice) device2 = NULL;
	g_autoptr(FwupdRelease) release = fwupd_release_new();

	fwupd_device_set_id(device, "0000000000000000000000000000000000000000");
	fwupd_device_set_parent_id(device, "1111111111111111111111111111111111111111");
	fwupd_device_set_name(device, "name");
	fwupd_device_set_version(device, "1.2.3");
	fwupd_device_set_plugin(device, "plugin");
	fwupd_device_set_serial(device, "123456");
	fwupd_device_set_summary(device, "summary");
	fwupd_device_set_details_url(device, "https://hughski.com");
	fwupd_device_set_branch(device, "stable");
	fwupd_device_set_vendor(device, "Hughski Limited");
	fwupd_device_set_version_lowest(device, "1.0.0");
	fwupd_device_set_version_highest(device, "2.0.0");
	fwupd_device_set_version_bootloader(device, "0.1.2");
	fwupd_device_set_update_error(device, "failed to update");
	fwupd_device_add_instance_id(device, "USB\\VID_273F&PID_1004");
	fwupd_device_add_guid(device, "2082b5e0-7a64-478a-b1b2-e3404fab6dad");
	fwupd_device_add_protocol(device, "com.hughski.colorhug");
	fwupd_device_add_issue(device, "CVE-2023-1234");
	fwupd_device_add_problem(device, FWUPD_DEVICE_PROBLEM_POWER_TOO_LOW);
	fwupd_device_add_checksum(device, "deadbeef");
	fwupd_device_add_vendor_id(device, "USB:0x273F");
	fwupd_device_add_icon(device, "computer");
	fwupd_release_set_version(release, "1.2.4");
	fwupd_device_add_release(device, release);
	fwupd_device_set_flags(device, FWUPD_DEVICE_FLAG_UPDATABLE);
	fwupd_device_add_request_flag(device, FWUPD_REQUEST_FLAG_ALLOW_GENERIC_MESSAGE);
	fwupd_device_set_version_format(device, FWUPD_VERSION_FORMAT_TRIPLET);
	fwupd_device_set_flashes_left(device, 3);
	fwupd_device_set_battery_level(device, 50);
	fwupd_device_set_battery_threshold(device, 10);
	fwupd_device_set_version_raw(device, 0x00010002);
	fwupd_device_set_version_lowest_raw(device, 0x00010000);
	fwupd_device_set_version_highest_raw(device, 0x00020000);
	fwupd_device_set_version_bootloader_raw(device, 0x00000102);
	fwupd_device_set_version_build_date(device, 0x1234);
	fwupd_device_set_install_duration(device, 120);
	fwupd_device_set_created(device, 0x1111);
	fwupd_device_set_modified(device, 0x2222);
	fwupd_device_set_update_state(device, FWUPD_UPDATE_STATE_SUCCESS);
	fwupd_device_set_percentage(device, 75);
	fwupd_device_set_status(device, FWUPD_STATUS_DEVICE_WRITE);

	r = fu_binder_device_to_aidl(device);
	device2 = fu_binder_device_from_aidl(r);

	g_assert_cmpstr(fwupd_device_get_id(device2),
			==,
			"0000000000000000000000000000000000000000");
	g_assert_cmpstr(fwupd_device_get_parent_id(device2),
			==,
			"1111111111111111111111111111111111111111");
	g_assert_cmpstr(fwupd_device_get_name(device2), ==, "name");
	g_assert_cmpstr(fwupd_device_get_version(device2), ==, "1.2.3");
	g_assert_cmpstr(fwupd_device_get_plugin(device2), ==, "plugin");
	g_assert_cmpstr(fwupd_device_get_serial(device2), ==, "123456");
	g_assert_cmpstr(fwupd_device_get_summary(device2), ==, "summary");
	g_assert_cmpstr(fwupd_device_get_details_url(device2), ==, "https://hughski.com");
	g_assert_cmpstr(fwupd_device_get_branch(device2), ==, "stable");
	g_assert_cmpstr(fwupd_device_get_vendor(device2), ==, "Hughski Limited");
	g_assert_cmpstr(fwupd_device_get_version_lowest(device2), ==, "1.0.0");
	g_assert_cmpstr(fwupd_device_get_version_highest(device2), ==, "2.0.0");
	g_assert_cmpstr(fwupd_device_get_version_bootloader(device2), ==, "0.1.2");
	g_assert_cmpstr(fwupd_device_get_update_error(device2), ==, "failed to update");
	g_assert_cmpint(fwupd_device_get_instance_ids(device2)->len, ==, 1);
	g_assert_cmpint(fwupd_device_get_guids(device2)->len, ==, 1);
	g_assert_cmpint(fwupd_device_get_protocols(device2)->len, ==, 1);
	g_assert_cmpint(fwupd_device_get_issues(device2)->len, ==, 1);
	g_assert_true(fwupd_device_has_problem(device2, FWUPD_DEVICE_PROBLEM_POWER_TOO_LOW));
	g_assert_cmpint(fwupd_device_get_checksums(device2)->len, ==, 1);
	g_assert_cmpint(fwupd_device_get_vendor_ids(device2)->len, ==, 1);
	g_assert_cmpint(fwupd_device_get_icons(device2)->len, ==, 1);
	g_assert_cmpint(fwupd_device_get_releases(device2)->len, ==, 1);
	g_assert_true(fwupd_device_has_flag(device2, FWUPD_DEVICE_FLAG_UPDATABLE));
	g_assert_cmpint(fwupd_device_get_version_format(device2), ==, FWUPD_VERSION_FORMAT_TRIPLET);
	g_assert_cmpint(fwupd_device_get_flashes_left(device2), ==, 3);
	g_assert_cmpint(fwupd_device_get_battery_level(device2), ==, 50);
	g_assert_cmpint(fwupd_device_get_battery_threshold(device2), ==, 10);
	g_assert_cmpint(fwupd_device_get_version_raw(device2), ==, 0x00010002);
	g_assert_cmpint(fwupd_device_get_version_lowest_raw(device2), ==, 0x00010000);
	g_assert_cmpint(fwupd_device_get_version_highest_raw(device2), ==, 0x00020000);
	g_assert_cmpint(fwupd_device_get_version_bootloader_raw(device2), ==, 0x00000102);
	g_assert_cmpint(fwupd_device_get_version_build_date(device2), ==, 0x1234);
	g_assert_cmpint(fwupd_device_get_install_duration(device2), ==, 120);
	g_assert_cmpint(fwupd_device_get_created(device2), ==, 0x1111);
	g_assert_cmpint(fwupd_device_get_modified(device2), ==, 0x2222);
	g_assert_cmpint(fwupd_device_get_update_state(device2), ==, FWUPD_UPDATE_STATE_SUCCESS);
	g_assert_cmpint(fwupd_device_get_percentage(device2), ==, 75);
	g_assert_cmpint(fwupd_device_get_status(device2), ==, FWUPD_STATUS_DEVICE_WRITE);
}

static void
fu_binder_request_func(void)
{
	aidl_fwupd::FwupdRequest r;
	g_autoptr(FwupdRequest) request = fwupd_request_new();

	fwupd_request_set_id(request, FWUPD_REQUEST_ID_REMOVE_REPLUG);
	fwupd_request_set_kind(request, FWUPD_REQUEST_KIND_IMMEDIATE);
	fwupd_request_set_message(request, "message");

	r = fu_binder_request_to_aidl(request);
	g_assert_cmpstr(r.id.c_str(), ==, FWUPD_REQUEST_ID_REMOVE_REPLUG);
	g_assert_cmpint(r.kind, ==, FWUPD_REQUEST_KIND_IMMEDIATE);
	g_assert_cmpstr(r.message.value().c_str(), ==, "message");
}

int
main(int argc, char **argv)
{
	g_test_init(&argc, &argv, NULL);
	g_test_add_func("/fwupd/binder/remote", fu_binder_remote_func);
	g_test_add_func("/fwupd/binder/release", fu_binder_release_func);
	g_test_add_func("/fwupd/binder/device", fu_binder_device_func);
	g_test_add_func("/fwupd/binder/request", fu_binder_request_func);
	return g_test_run();
}
