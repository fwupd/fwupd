/*
 * Copyright 2017 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include "fwupd-device.h"
#include "fwupd-enums.h"
#include "fwupd-error.h"
#include "fwupd-plugin.h"
#include "fwupd-release.h"
#include "fwupd-remote.h"
#include "fwupd-request.h"
#include "fwupd-test.h"

static void
fwupd_enums_func(void)
{
	/* enums */
	for (guint i = 0; i < FWUPD_ERROR_LAST; i++) {
		const gchar *tmp = fwupd_error_to_string(i);
		g_assert_cmpstr(tmp, !=, NULL);
		g_assert_cmpint(fwupd_error_from_string(tmp), ==, i);
	}
	for (guint i = FWUPD_STATUS_UNKNOWN + 1; i < FWUPD_STATUS_LAST; i++) {
		const gchar *tmp = fwupd_status_to_string(i);
		g_assert_cmpstr(tmp, !=, NULL);
		g_assert_cmpint(fwupd_status_from_string(tmp), ==, i);
	}
	for (guint i = FWUPD_UPDATE_STATE_UNKNOWN + 1; i < FWUPD_UPDATE_STATE_LAST; i++) {
		const gchar *tmp = fwupd_update_state_to_string(i);
		g_assert_cmpstr(tmp, !=, NULL);
		g_assert_cmpint(fwupd_update_state_from_string(tmp), ==, i);
	}
	for (guint i = FWUPD_REQUEST_KIND_UNKNOWN + 1; i < FWUPD_REQUEST_KIND_LAST; i++) {
		const gchar *tmp = fwupd_request_kind_to_string(i);
		g_assert_cmpstr(tmp, !=, NULL);
		g_assert_cmpint(fwupd_request_kind_from_string(tmp), ==, i);
	}
	for (guint i = FWUPD_RELEASE_URGENCY_UNKNOWN + 1; i < FWUPD_RELEASE_URGENCY_LAST; i++) {
		const gchar *tmp = fwupd_release_urgency_to_string(i);
		g_assert_cmpstr(tmp, !=, NULL);
		g_assert_cmpint(fwupd_release_urgency_from_string(tmp), ==, i);
	}
	for (guint i = 1; i < FWUPD_VERSION_FORMAT_LAST; i++) {
		const gchar *tmp = fwupd_version_format_to_string(i);
		g_assert_cmpstr(tmp, !=, NULL);
		g_assert_cmpint(fwupd_version_format_from_string(tmp), ==, i);
	}
	for (guint i = 1; i < FWUPD_REMOTE_KIND_LAST; i++) {
		const gchar *tmp = fwupd_remote_kind_to_string(i);
		g_assert_cmpstr(tmp, !=, NULL);
		g_assert_cmpint(fwupd_remote_kind_from_string(tmp), ==, i);
	}

	/* bitfield */
	for (guint64 i = 1; i <= FWUPD_DEVICE_FLAG_INSTALL_SKIP_VERSION_CHECK; i *= 2) {
		const gchar *tmp = fwupd_device_flag_to_string(i);
		if (tmp == NULL)
			continue;
		g_assert_cmpint(fwupd_device_flag_from_string(tmp), ==, i);
	}
	for (guint64 i = 1; i <= FWUPD_DEVICE_PROBLEM_IN_USE; i *= 2) {
		const gchar *tmp = fwupd_device_problem_to_string(i);
		g_assert_cmpstr(tmp, !=, NULL);
		g_assert_cmpint(fwupd_device_problem_from_string(tmp), ==, i);
	}
	for (guint64 i = 1; i <= FWUPD_PLUGIN_FLAG_TEST_ONLY; i *= 2) {
		const gchar *tmp = fwupd_plugin_flag_to_string(i);
		g_assert_cmpstr(tmp, !=, NULL);
		g_assert_cmpint(fwupd_plugin_flag_from_string(tmp), ==, i);
	}
	for (guint64 i = 1; i <= FWUPD_FEATURE_FLAG_REQUESTS_NON_GENERIC; i *= 2) {
		const gchar *tmp = fwupd_feature_flag_to_string(i);
		g_assert_cmpstr(tmp, !=, NULL);
		g_assert_cmpint(fwupd_feature_flag_from_string(tmp), ==, i);
	}
	for (guint64 i = 1; i <= FWUPD_RELEASE_FLAG_TRUSTED_REPORT; i *= 2) {
		const gchar *tmp = fwupd_release_flag_to_string(i);
		g_assert_cmpstr(tmp, !=, NULL);
		g_assert_cmpint(fwupd_release_flag_from_string(tmp), ==, i);
	}
	for (guint64 i = 1; i <= FWUPD_REQUEST_FLAG_NON_GENERIC_IMAGE; i *= 2) {
		const gchar *tmp = fwupd_request_flag_to_string(i);
		g_assert_cmpstr(tmp, !=, NULL);
		g_assert_cmpint(fwupd_request_flag_from_string(tmp), ==, i);
	}
	for (guint64 i = 1; i < FWUPD_REMOTE_FLAG_ALLOW_P2P_FIRMWARE; i *= 2) {
		const gchar *tmp = fwupd_remote_flag_to_string(i);
		if (tmp == NULL)
			break;
		g_assert_cmpint(fwupd_remote_flag_from_string(tmp), ==, i);
	}
	for (guint64 i = 1; i <= FWUPD_INSTALL_FLAG_ONLY_EMULATED; i *= 2) {
		const gchar *tmp = fwupd_install_flags_to_string(i);
		if (tmp == NULL)
			continue;
		g_assert_cmpint(fwupd_install_flags_from_string(tmp), ==, i);
	}
}

int
main(int argc, char **argv)
{
	g_test_init(&argc, &argv, NULL);
	g_assert_cmpint(sizeof(FwupdDeviceFlags), ==, sizeof(guint64));
	g_assert_cmpint(sizeof(FwupdStatus), ==, sizeof(guint32));
	g_test_add_func("/fwupd/enums", fwupd_enums_func);
	return g_test_run();
}
