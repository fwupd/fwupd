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
	gboolean ret;
	g_autoptr(FwupdRemote) remote = fwupd_remote_new();
	g_autoptr(FwupdRemote) remote2 = NULL;
	g_autoptr(GError) error = NULL;
	g_autofree gchar *str = NULL;

	fwupd_remote_set_id(remote, "id");
	fwupd_remote_set_title(remote, "title");
	r = fu_binder_remote_to_aidl(remote);
	remote2 = fu_binder_remote_from_aidl(r);
	g_assert_cmpstr(fwupd_remote_get_id(remote2), ==, "id");
	g_assert_cmpstr(fwupd_remote_get_title(remote2), ==, "title");
	str = fwupd_codec_to_string(FWUPD_CODEC(remote2));
	g_assert_nonnull(str);
	ret = fu_test_compare_lines(str,
				    "FwupdRemote:\n"
				    "    Id:                 id\n"
				    "    Title:              title\n"
				    "    Flags:              0\n"
				    "    Enabled:            false\n"
				    "    ApprovalRequired:   false\n"
				    "    AutomaticReports:   false\n"
				    "    AutomaticSecurityReports: false\n"
				    "    Priority:           0\n"
				    "    Mtime:              0\n"
				    "    RefreshInterval:    0\n",
				    &error);
	g_assert_no_error(error);
	g_assert_true(ret);
}

static void
fu_binder_release_func(void)
{
	aidl_fwupd::FwupdRelease r;
	gboolean ret;
	g_autoptr(FwupdRelease) release = fwupd_release_new();
	g_autoptr(FwupdRelease) release2 = NULL;
	g_autoptr(GError) error = NULL;
	g_autofree gchar *str = NULL;

	fwupd_release_set_id(release, "id");
	fwupd_release_add_location(release, "http://foo");
	r = fu_binder_release_to_aidl(release);
	release2 = fu_binder_release_from_aidl(r);
	g_assert_cmpstr(fwupd_release_get_id(release2), ==, "id");
	g_assert_cmpint(fwupd_release_get_locations(release2)->len, ==, 1);
	str = fwupd_codec_to_string(FWUPD_CODEC(release2));
	g_assert_nonnull(str);
	ret = fu_test_compare_lines(str,
				    "FwupdRelease:\n"
				    "  ReleaseId:            id\n"
				    "  Name:\n" // FIXME check if empty and not set
				    "  Version:\n"
				    "  Filename:\n"
				    "  Uri:                  http://foo\n"
				    "  Flags:                none\n",
				    &error);
	g_assert_no_error(error);
	g_assert_true(ret);
}

static void
fu_binder_device_func(void)
{
	aidl_fwupd::FwupdDevice r;
	gboolean ret;
	g_autoptr(FwupdDevice) device = fwupd_device_new();
	g_autoptr(FwupdDevice) device2 = NULL;
	g_autoptr(GError) error = NULL;
	g_autofree gchar *str = NULL;

	fwupd_device_set_id(device, "0000000000000000000000000000000000000000");
	fwupd_device_set_version(device, "1.2.3");
	r = fu_binder_device_to_aidl(device);
	device2 = fu_binder_device_from_aidl(r);
	str = fwupd_codec_to_string(FWUPD_CODEC(device2));
	g_assert_nonnull(str);
	ret = fu_test_compare_lines(
	    str,
	    "FwupdDevice:\n"
	    "  DeviceId:             0000000000000000000000000000000000000000\n"
	    "  CompositeId:          0000000000000000000000000000000000000000\n"
	    "  Flags:                none\n"
	    "  Version:              1.2.3\n",
	    &error);
	g_assert_no_error(error);
	g_assert_true(ret);
}

int
main(int argc, char **argv)
{
	g_test_init(&argc, &argv, NULL);
	g_test_add_func("/fwupd/binder/remote", fu_binder_remote_func);
	g_test_add_func("/fwupd/binder/release", fu_binder_release_func);
	g_test_add_func("/fwupd/binder/device", fu_binder_device_func);
	return g_test_run();
}
