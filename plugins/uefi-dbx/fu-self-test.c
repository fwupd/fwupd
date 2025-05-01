/*
 * Copyright 2020 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include <curl/curl.h>
#include <curl/easy.h>

#include "fwupd-error.h"

#include "fu-context-private.h"
#include "fu-device-private.h"
#include "fu-plugin-private.h"
#include "fu-uefi-dbx-device.h"
#include "fu-uefi-dbx-plugin.h"
#include "fu-uefi-device-private.h"

static void
fu_efi_image_func(void)
{
	struct {
		const gchar *basename;
		const gchar *checksum;
	} map[] = {
	    {"bootmgr.efi", "fd26aad248cc1e21e0c6b453212b2b309f7e221047bf22500ed0f8ce30bd1610"},
	    {"fwupdx64-2.efi", "6e0f01e7018c90a1e3d24908956fbeffd29a620c6c5f3ffa3feb2f2802ed4448"},
	};
	for (guint i = 0; i < G_N_ELEMENTS(map); i++) {
		gboolean ret;
		g_autofree gchar *csum = NULL;
		g_autofree gchar *fn = NULL;
		g_autoptr(FuFirmware) firmware = fu_pefile_firmware_new();
		g_autoptr(GError) error = NULL;
		g_autoptr(GFile) file = NULL;

		fn = g_test_build_filename(G_TEST_DIST, "tests", map[i].basename, NULL);
		file = g_file_new_for_path(fn);
		if (!g_file_query_exists(file, NULL)) {
			g_autofree gchar *msg =
			    g_strdup_printf("failed to find file %s", map[i].basename);
			g_test_skip(msg);
			return;
		}
		ret = fu_firmware_parse_file(firmware, file, FU_FIRMWARE_PARSE_FLAG_NONE, &error);
		g_assert_no_error(error);
		g_assert_true(ret);

		csum = fu_firmware_get_checksum(firmware, G_CHECKSUM_SHA256, &error);
		g_assert_no_error(error);
		g_assert_nonnull(csum);
		g_assert_cmpstr(csum, ==, map[i].checksum);
	}
}

typedef struct {
	gboolean running_in_snap;
	gboolean snapd_fde_detected;
	gboolean snapd_supported;
	const gchar *mock_snapd_scenario;
} FuTestCase;

typedef struct {
	FuContext *ctx;

	gboolean mock_snapd_available;

	CURL *mock_snapd_curl;
	struct curl_slist *mock_curl_hdrs;
} FuTestFixture;

static gboolean
fu_self_test_mock_snapd_easy_post_request(FuTestFixture *fixture,
					  const gchar *endpoint,
					  const gchar *data,
					  gsize len)
{
	CURL *curl = curl_easy_duphandle(fixture->mock_snapd_curl);
	CURLcode res = -1;
	glong status_code = 0;
	g_autofree gchar *endpoint_str = g_strdup_printf("http://localhost%s", endpoint);

	g_debug("mock snapd request to %s with data: '%s'", endpoint_str, data);

	/* use snap dedicated socket when running inside a snap */
	(void)curl_easy_setopt(curl, CURLOPT_URL, endpoint_str);
	(void)curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, len);
	(void)curl_easy_setopt(curl, CURLOPT_POSTFIELDS, data);
	res = curl_easy_perform(curl);
	g_debug("curl error: %u %s", res, curl_easy_strerror(res));
	curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &status_code);
	curl_easy_cleanup(curl);
	return res == CURLE_OK && status_code == 200;
}

static size_t
fu_self_test_curl_write_callback(char *ptr, size_t size, size_t nmemb, void *userdata)
{
	GByteArray *bufarr = (GByteArray *)userdata;
	gsize sz = size * nmemb;
	g_byte_array_append(bufarr, (const guint8 *)ptr, sz);
	return sz;
}

static GBytes *
fu_self_test_mock_snapd_easy_get_request(FuTestFixture *fixture, const gchar *endpoint)
{
	CURL *curl = curl_easy_duphandle(fixture->mock_snapd_curl);
	CURLcode res = -1;
	glong status_code = 0;
	g_autoptr(GByteArray) buf = g_byte_array_new();
	g_autofree gchar *endpoint_str = g_strdup_printf("http://localhost%s", endpoint);

	/* use snap dedicated socket when running inside a snap */
	(void)curl_easy_setopt(curl, CURLOPT_URL, endpoint_str);
	(void)curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, fu_self_test_curl_write_callback);
	(void)curl_easy_setopt(curl, CURLOPT_WRITEDATA, buf);
	res = curl_easy_perform(curl);
	g_debug("curl error: %u %s", res, curl_easy_strerror(res));
	curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &status_code);
	curl_easy_cleanup(curl);

	g_assert_true(res == CURLE_OK);
	g_assert_true(status_code == 200);

	g_debug("rsp:\n%s", buf->data);

	return g_bytes_new(buf->data, buf->len);
}

typedef struct {
	guint startup;
	guint prepare;
	guint cleanup;
} FuTestSnapdCalls;

static void
fu_self_test_mock_snapd_assert_calls(FuTestFixture *fixture, FuTestSnapdCalls calls)
{
	guint64 val = 0xffffff;
	g_autoptr(GBytes) rsp = NULL;
	g_autoptr(GKeyFile) kf = g_key_file_new();
	g_autoptr(GError) error = NULL;

	rsp = fu_self_test_mock_snapd_easy_get_request(fixture, "/test/stats");

	g_key_file_load_from_bytes(kf, rsp, 0, &error);
	g_assert_no_error(error);

	val = g_key_file_get_uint64(kf, "stats", "efi-secureboot-update-startup", &error);
	g_assert_no_error(error);
	g_assert_cmpuint(val, ==, calls.startup);

	val = g_key_file_get_uint64(kf, "stats", "efi-secureboot-update-db-prepare", &error);
	g_assert_no_error(error);
	g_assert_cmpuint(val, ==, calls.prepare);

	val = g_key_file_get_uint64(kf, "stats", "efi-secureboot-update-db-cleanup", &error);
	g_assert_no_error(error);
	g_assert_cmpuint(val, ==, calls.cleanup);
}

static gboolean
fu_self_test_mock_snapd_reset(FuTestFixture *fixture)
{
	return fu_self_test_mock_snapd_easy_post_request(fixture, "/test/reset", NULL, 0);
}

static gboolean
fu_self_test_mock_snapd_setup_scenario(FuTestFixture *fixture, const gchar *scenario)
{
	g_autofree gchar *scenario_request = g_strdup_printf("{\"scenario\":\"%s\"}", scenario);

	return fu_self_test_mock_snapd_easy_post_request(fixture,
							 "/test/setup",
							 scenario_request,
							 strlen(scenario_request));
}

static gboolean
fu_self_test_mock_snapd_init(FuTestFixture *fixture)
{
	CURL *curl = curl_easy_init();
	struct curl_slist *req_hdrs = NULL;
	const char *mock_snapd_snap_socket = g_getenv("FWUPD_SNAPD_SNAP_SOCKET");

	g_assert_nonnull(mock_snapd_snap_socket);

	/* use snap dedicated socket when running inside a snap */
	(void)curl_easy_setopt(curl, CURLOPT_UNIX_SOCKET_PATH, mock_snapd_snap_socket);
	(void)curl_easy_setopt(curl, CURLOPT_VERBOSE, 1L);
	req_hdrs = curl_slist_append(req_hdrs, "Content-Type: application/json");
	(void)curl_easy_setopt(curl, CURLOPT_HTTPHEADER, req_hdrs);

	fixture->mock_snapd_curl = curl;
	fixture->mock_curl_hdrs = req_hdrs;

	return fu_self_test_mock_snapd_reset(fixture);
}

static void
fu_self_test_set_up(FuTestFixture *fixture, gconstpointer user_data)
{
	FuTestCase *tc = (FuTestCase *)user_data;
	gboolean ret;
	g_autoptr(GError) error = NULL;

	fixture->ctx = fu_context_new();

	if ((tc->running_in_snap || tc->snapd_fde_detected) &&
	    fu_self_test_mock_snapd_init(fixture)) {
		fixture->mock_snapd_available = TRUE;

		if (tc->running_in_snap)
			(void)g_setenv("SNAP", "fwupd", TRUE);
		if (tc->snapd_fde_detected)
			fu_context_add_flag(fixture->ctx, FU_CONTEXT_FLAG_FDE_SNAPD);

		fu_self_test_mock_snapd_setup_scenario(fixture, tc->mock_snapd_scenario);
	}

	fu_context_add_flag(fixture->ctx, FU_CONTEXT_FLAG_INHIBIT_VOLUME_MOUNT);
	ret = fu_context_load_quirks(fixture->ctx,
				     FU_QUIRKS_LOAD_FLAG_NO_CACHE | FU_QUIRKS_LOAD_FLAG_NO_VERIFY,
				     &error);
	g_assert_no_error(error);
	g_assert_true(ret);
}

static void
fu_self_test_tear_down(FuTestFixture *fixture, gconstpointer user_data)
{
	FuTestCase *tc = (FuTestCase *)user_data;
	if (tc->running_in_snap)
		g_unsetenv("SNAP");
	if (tc->running_in_snap || tc->snapd_fde_detected) {
		if (fixture->mock_snapd_available)
			fu_self_test_mock_snapd_reset(fixture);

		if (fixture->mock_snapd_curl)
			curl_easy_cleanup(fixture->mock_snapd_curl);

		if (fixture->mock_curl_hdrs)
			curl_slist_free_all(fixture->mock_curl_hdrs);
	}

	g_object_unref(fixture->ctx);
}

static gboolean
fu_test_mock_efivar_content(FuEfivars *efivars,
			    const gchar *guid,
			    const gchar *name,
			    const gchar *path,
			    GError **error)
{
	gchar *mock_blob = NULL;
	gsize mock_blob_size = 0;
	g_autoptr(GBytes) mock_bytes = NULL;

	if (!g_file_get_contents(path, &mock_blob, &mock_blob_size, error))
		return FALSE;

	mock_bytes = g_bytes_new_take(mock_blob, mock_blob_size);

	return fu_efivars_set_data_bytes(efivars, guid, name, mock_bytes, 0, error);
}

static gboolean
fu_test_mock_dbx_efivars(FuEfivars *efivars, GError **error)
{
	g_autofree gchar *mock_kek_path =
	    g_test_build_filename(G_TEST_DIST,
				  "tests/KEK-8be4df61-93ca-11d2-aa0d-00e098032b8c",
				  NULL);
	g_autofree gchar *mock_dbx_path =
	    g_test_build_filename(G_TEST_DIST,
				  "tests/dbx-d719b2cb-3d3a-4596-a3bc-dad00e67656f",
				  NULL);

	if (!fu_test_mock_efivar_content(efivars,
					 FU_EFIVARS_GUID_EFI_GLOBAL,
					 "KEK",
					 mock_kek_path,
					 error))
		return FALSE;

	return fu_test_mock_efivar_content(efivars,
					   FU_EFIVARS_GUID_SECURITY_DATABASE,
					   "dbx",
					   mock_dbx_path,
					   error);
}

static FuFirmware *
fu_test_mock_dbx_update_firmware(void)
{
	gboolean ret;
	gchar *mock_blob = NULL;
	gsize mock_blob_size = 0;
	g_autoptr(GError) error = NULL;
	g_autoptr(GBytes) mock_bytes = NULL;
	g_autofree gchar *mock_dbx_update_path =
	    g_test_build_filename(G_TEST_DIST, "tests/dbx-update.auth", NULL);

	ret = g_file_get_contents(mock_dbx_update_path, &mock_blob, &mock_blob_size, &error);
	g_assert_no_error(error);
	g_assert_true(ret);

	mock_bytes = g_bytes_new_take(mock_blob, mock_blob_size);
	return fu_firmware_new_from_bytes(mock_bytes);
}

static void
fu_uefi_dbx_test_plugin_update(FuTestFixture *fixture, gconstpointer user_data)
{
	/* run though an update */
	FuTestCase *tc = (FuTestCase *)user_data;
	gboolean ret;
	FuContext *ctx = fixture->ctx;
	FuEfivars *efivars = fu_context_get_efivars(ctx);
	g_autoptr(GError) error = NULL;
	g_autoptr(FuFirmware) firmware = NULL;
	g_autoptr(FuProgress) progress = fu_progress_new(G_STRLOC);
	g_autoptr(FuPlugin) plugin = fu_plugin_new_from_gtype(fu_uefi_dbx_plugin_get_type(), ctx);
	g_autoptr(FuUefiDbxDevice) uefi_device = NULL;
	gboolean expect_snapd_calls = tc->running_in_snap || tc->snapd_fde_detected;

	/* progress */
	fu_progress_add_flag(progress, FU_PROGRESS_FLAG_NO_PROFILE);
	fu_progress_add_step(progress, FWUPD_STATUS_LOADING, 33, NULL);
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_WRITE, 33, NULL);
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_VERIFY, 33, NULL);

	if (expect_snapd_calls && !fixture->mock_snapd_available) {
		g_test_skip("mock snapd not available");
		return;
	}

	if (!fu_test_mock_dbx_efivars(efivars, &error) &&
	    g_error_matches(error, G_FILE_ERROR, G_FILE_ERROR_NOENT)) {
		g_test_skip("test assets unavailable");
		return;
	}
	g_assert_no_error(error);

	ret = fu_plugin_runner_startup(plugin, fu_progress_get_child(progress), &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	fu_progress_step_done(progress);

	uefi_device = g_object_new(FU_TYPE_UEFI_DBX_DEVICE, "context", ctx, NULL);
	fu_uefi_device_set_guid(FU_UEFI_DEVICE(uefi_device), FU_EFIVARS_GUID_EFI_GLOBAL);
	fu_uefi_device_set_name(FU_UEFI_DEVICE(uefi_device), "KEK");
	ret = fu_plugin_runner_device_created(plugin, FU_DEVICE(uefi_device), &error);
	g_assert_no_error(error);
	g_assert_true(ret);

	firmware = fu_test_mock_dbx_update_firmware();
	ret = fu_plugin_runner_write_firmware(plugin,
					      FU_DEVICE(uefi_device),
					      firmware,
					      fu_progress_get_child(progress),
					      /* skip verification of ESP binaries*/
					      FWUPD_INSTALL_FLAG_FORCE,
					      &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	g_assert_true(fu_device_has_flag(FU_DEVICE(uefi_device), FWUPD_DEVICE_FLAG_NEEDS_REBOOT));
	fu_progress_step_done(progress);

	/* this is normally invoked by FuEngine */
	ret = fu_device_cleanup(FU_DEVICE(uefi_device), fu_progress_get_child(progress), 0, &error);
	g_assert_true(ret);
	g_assert_no_error(error);
	fu_progress_step_done(progress);

	if (expect_snapd_calls) {
		fu_self_test_mock_snapd_assert_calls(fixture,
						     (FuTestSnapdCalls){
							 .startup = 1,
							 .prepare = 1,
							 .cleanup = 1,
						     });
	}
}

static void
fu_uefi_dbx_test_plugin_failed_update(FuTestFixture *fixture, gconstpointer user_data)
{
	/* update which fails at either write or cleanup steps, this test can only
	 * properly mock the environment when using snapd integration */

	FuTestCase *tc = (FuTestCase *)user_data;
	gboolean ret;
	FuContext *ctx = fixture->ctx;
	FuEfivars *efivars = fu_context_get_efivars(ctx);
	g_autoptr(GError) error = NULL;
	g_autoptr(FuFirmware) firmware = NULL;
	g_autoptr(FuProgress) progress = fu_progress_new(G_STRLOC);
	g_autoptr(FuProgress) progress_write = fu_progress_new(G_STRLOC);
	g_autoptr(FuPlugin) plugin = fu_plugin_new_from_gtype(fu_uefi_dbx_plugin_get_type(), ctx);
	g_autoptr(FuUefiDbxDevice) uefi_device = NULL;

	if (!tc->running_in_snap && !tc->snapd_fde_detected) {
		g_test_skip("only supports snapd integration variant");
		return;
	}

	if (!fixture->mock_snapd_available) {
		g_test_skip("mock snapd not available");
		return;
	}

	if (!fu_test_mock_dbx_efivars(efivars, &error) &&
	    g_error_matches(error, G_FILE_ERROR, G_FILE_ERROR_NOENT)) {
		g_test_skip("test assets unavailable");
		return;
	}
	g_assert_no_error(error);

	ret = fu_plugin_runner_startup(plugin, progress, &error);
	g_assert_no_error(error);
	g_assert_true(ret);

	uefi_device = g_object_new(FU_TYPE_UEFI_DBX_DEVICE, "context", ctx, NULL);
	fu_uefi_device_set_guid(FU_UEFI_DEVICE(uefi_device), FU_EFIVARS_GUID_EFI_GLOBAL);
	fu_uefi_device_set_name(FU_UEFI_DEVICE(uefi_device), "KEK");
	ret = fu_plugin_runner_device_created(plugin, FU_DEVICE(uefi_device), &error);
	g_assert_no_error(error);
	g_assert_true(ret);

	firmware = fu_test_mock_dbx_update_firmware();
	ret = fu_plugin_runner_write_firmware(plugin,
					      FU_DEVICE(uefi_device),
					      firmware,
					      progress_write,
					      /* skip verification of ESP binaries*/
					      FWUPD_INSTALL_FLAG_FORCE,
					      &error);
	if (g_str_equal(tc->mock_snapd_scenario, "failed-prepare")) {
		g_assert_error(error, FWUPD_ERROR, FWUPD_ERROR_INTERNAL);
		g_clear_error(&error);
	} else {
		g_assert_no_error(error);
		g_assert_true(ret);
	}

	/* engine invokes cleanup */
	ret = fu_device_cleanup(FU_DEVICE(uefi_device), progress, 0, &error);
	if (g_str_equal(tc->mock_snapd_scenario, "failed-cleanup")) {
		g_assert_error(error, FWUPD_ERROR, FWUPD_ERROR_INTERNAL);
	} else {
		g_assert_no_error(error);
		g_assert_true(ret);
	}

	fu_self_test_mock_snapd_assert_calls(fixture,
					     (FuTestSnapdCalls){
						 .startup = 1,
						 .prepare = 1,
						 .cleanup = 1,
					     });
}

static void
fu_uefi_dbx_test_plugin_coldplug_probed_device(FuTestFixture *fixture, gconstpointer user_data)
{
	FuTestCase *tc = (FuTestCase *)user_data;
	gboolean ret;
	FuContext *ctx = fixture->ctx;
	FuEfivars *efivars = fu_context_get_efivars(ctx);
	g_autoptr(GError) error = NULL;
	g_autoptr(FuProgress) progress = fu_progress_new(G_STRLOC);
	g_autoptr(FuPlugin) plugin = fu_plugin_new_from_gtype(fu_uefi_dbx_plugin_get_type(), ctx);
	g_autoptr(FuUefiDbxDevice) uefi_device = NULL;
	gboolean expect_snapd_calls = tc->running_in_snap || tc->snapd_fde_detected;

	if (expect_snapd_calls && !fixture->mock_snapd_available) {
		g_test_skip("mock snapd not available");
		return;
	}

	if (!fu_test_mock_dbx_efivars(efivars, &error) &&
	    g_error_matches(error, G_FILE_ERROR, G_FILE_ERROR_NOENT)) {
		g_test_skip("test assets unavailable");
		return;
	}
	g_assert_no_error(error);

	ret = fu_plugin_runner_startup(plugin, progress, &error);
	g_assert_no_error(error);
	g_assert_true(ret);

	uefi_device = g_object_new(FU_TYPE_UEFI_DBX_DEVICE, "context", ctx, NULL);
	fu_uefi_device_set_guid(FU_UEFI_DEVICE(uefi_device), FU_EFIVARS_GUID_EFI_GLOBAL);
	fu_uefi_device_set_name(FU_UEFI_DEVICE(uefi_device), "KEK");
	ret = fu_plugin_runner_device_created(plugin, FU_DEVICE(uefi_device), &error);
	g_assert_no_error(error);
	g_assert_true(ret);

	ret = fu_device_has_inhibit(FU_DEVICE(uefi_device), "no-snapd-dbx");
	if (expect_snapd_calls && tc->snapd_supported &&
	    g_str_equal(tc->mock_snapd_scenario, "failed-startup")) {
		/* startup failed for whatever reason, device updates are inhibited */
		g_assert_true(ret);
	} else
		g_assert_false(ret);

	if (expect_snapd_calls) {
		fu_self_test_mock_snapd_assert_calls(fixture,
						     (FuTestSnapdCalls){
							 .startup = 1,
						     });
	}
}

static void
fu_uefi_dbx_test_plugin_startup(FuTestFixture *fixture, gconstpointer user_data)
{
	FuTestCase *tc = (FuTestCase *)user_data;
	gboolean ret;
	FuContext *ctx = fixture->ctx;
	g_autoptr(FuProgress) progress = fu_progress_new(G_STRLOC);
	g_autoptr(GError) error = NULL;
	g_autoptr(FuPlugin) plugin = fu_plugin_new_from_gtype(fu_uefi_dbx_plugin_get_type(), ctx);

	if (tc->running_in_snap && !fixture->mock_snapd_available) {
		g_test_skip("mock snapd not available");
		return;
	}

	ret = fu_context_load_quirks(ctx,
				     FU_QUIRKS_LOAD_FLAG_NO_CACHE | FU_QUIRKS_LOAD_FLAG_NO_VERIFY,
				     &error);
	g_assert_no_error(error);
	g_assert_true(ret);

	ret = fu_plugin_runner_startup(plugin, progress, &error);
	g_assert_no_error(error);
	g_assert_true(ret);

	if (tc->running_in_snap) {
		fu_self_test_mock_snapd_assert_calls(fixture,
						     (FuTestSnapdCalls){
							 .startup = 1,
						     });
	}
}

int
main(int argc, char **argv)
{
	FuTestCase simple = {
	    .running_in_snap = FALSE,
	};
	/* test variants with snapd, see tests/snapd.py for what a specific scenario
	 * supports */
	FuTestCase running_in_snap = {
	    .running_in_snap = TRUE,
	    .snapd_supported = TRUE,
	    .mock_snapd_scenario = "happy-startup",
	};
	FuTestCase snapd_fde_detected = {
	    .snapd_fde_detected = TRUE,
	    .snapd_supported = TRUE,
	    .mock_snapd_scenario = "happy-startup",
	};
	FuTestCase running_in_snap_snapd_fde_detected = {
	    .snapd_fde_detected = TRUE,
	    .running_in_snap = TRUE,
	    .snapd_supported = TRUE,
	    .mock_snapd_scenario = "happy-startup",
	};
	FuTestCase running_in_snap_no_support = {
	    .running_in_snap = TRUE,
	    .snapd_supported = FALSE,
	    .mock_snapd_scenario = "not-supported",
	};
	FuTestCase snapd_fde_detected_no_support = {
	    .snapd_fde_detected = TRUE,
	    .snapd_supported = FALSE,
	    .mock_snapd_scenario = "not-supported",
	};
	FuTestCase running_in_snap_bad_startup = {
	    .running_in_snap = TRUE,
	    .snapd_supported = TRUE,
	    .mock_snapd_scenario = "failed-startup",
	};
	FuTestCase snapd_fde_detected_bad_startup = {
	    .snapd_fde_detected = TRUE,
	    .snapd_supported = TRUE,
	    .mock_snapd_scenario = "failed-startup",
	};
	FuTestCase running_in_snap_update = {
	    .running_in_snap = TRUE,
	    .snapd_supported = TRUE,
	    .mock_snapd_scenario = "happy-update",
	};
	FuTestCase snapd_fde_detected_update = {
	    .snapd_fde_detected = TRUE,
	    .snapd_supported = TRUE,
	    .mock_snapd_scenario = "happy-update",
	};
	FuTestCase running_in_snap_update_failed_prepare = {
	    .running_in_snap = TRUE,
	    .snapd_supported = TRUE,
	    .mock_snapd_scenario = "failed-prepare",
	};
	FuTestCase snapd_fde_detected_update_failed_prepare = {
	    .snapd_fde_detected = TRUE,
	    .snapd_supported = TRUE,
	    .mock_snapd_scenario = "failed-prepare",
	};
	FuTestCase running_in_snap_update_failed_cleanup = {
	    .running_in_snap = TRUE,
	    .snapd_supported = TRUE,
	    .mock_snapd_scenario = "failed-cleanup",
	};
	FuTestCase snapd_fde_detected_update_failed_cleanup = {
	    .snapd_fde_detected = TRUE,
	    .snapd_supported = TRUE,
	    .mock_snapd_scenario = "failed-cleanup",
	};
	g_autofree gchar *testdatadir = NULL;

	(void)g_setenv("G_TEST_SRCDIR", SRCDIR, FALSE);
	g_test_init(&argc, &argv, NULL);

	/* only critical and error are fatal */
	g_log_set_fatal_mask(NULL, G_LOG_LEVEL_ERROR | G_LOG_LEVEL_CRITICAL);
	(void)g_setenv("G_MESSAGES_DEBUG", "all", TRUE);

	testdatadir = g_test_build_filename(G_TEST_DIST, "tests", NULL);

	(void)g_setenv("FWUPD_SYSFSFWDIR", testdatadir, TRUE);
	(void)g_setenv("FWUPD_EFIVARS", "dummy", TRUE);
	(void)g_setenv("FWUPD_SYSFSDRIVERDIR", testdatadir, TRUE);
	(void)g_setenv("FWUPD_SYSFSFWATTRIBDIR", testdatadir, TRUE);
	(void)g_setenv("FWUPD_SNAPD_SNAP_SOCKET", "/tmp/mock-snapd-test.sock", TRUE);

	/* tests go here */
	g_test_add_func("/uefi-dbx/image", fu_efi_image_func);

	g_test_add("/uefi-dbx/startup",
		   FuTestFixture,
		   &simple,
		   fu_self_test_set_up,
		   fu_uefi_dbx_test_plugin_startup,
		   fu_self_test_tear_down);
	g_test_add("/uefi-dbx/update",
		   FuTestFixture,
		   &simple,
		   fu_self_test_set_up,
		   fu_uefi_dbx_test_plugin_update,
		   fu_self_test_tear_down);

	g_test_add("/uefi-dbx/startup/snapd/running-in-snap/supported",
		   FuTestFixture,
		   &running_in_snap,
		   fu_self_test_set_up,
		   fu_uefi_dbx_test_plugin_startup,
		   fu_self_test_tear_down);
	g_test_add("/uefi-dbx/startup/snapd/fde-detected/supported",
		   FuTestFixture,
		   &snapd_fde_detected,
		   fu_self_test_set_up,
		   fu_uefi_dbx_test_plugin_startup,
		   fu_self_test_tear_down);
	g_test_add("/uefi-dbx/startup/snapd/running-in-snap-and-fde-detected/supported",
		   FuTestFixture,
		   &running_in_snap_snapd_fde_detected,
		   fu_self_test_set_up,
		   fu_uefi_dbx_test_plugin_startup,
		   fu_self_test_tear_down);
	g_test_add("/uefi-dbx/startup/snapd/running-in-snap/not-supported",
		   FuTestFixture,
		   &running_in_snap_no_support,
		   fu_self_test_set_up,
		   fu_uefi_dbx_test_plugin_startup,
		   fu_self_test_tear_down);
	g_test_add("/uefi-dbx/startup/snapd/fde-detected/not-supported",
		   FuTestFixture,
		   &snapd_fde_detected_no_support,
		   fu_self_test_set_up,
		   fu_uefi_dbx_test_plugin_startup,
		   fu_self_test_tear_down);
	g_test_add("/uefi-dbx/startup/snapd/running-in-snap/supported-failure",
		   FuTestFixture,
		   &running_in_snap_bad_startup,
		   fu_self_test_set_up,
		   fu_uefi_dbx_test_plugin_startup,
		   fu_self_test_tear_down);
	g_test_add("/uefi-dbx/startup/snapd/fde-detected/supported-failure",
		   FuTestFixture,
		   &snapd_fde_detected_bad_startup,
		   fu_self_test_set_up,
		   fu_uefi_dbx_test_plugin_startup,
		   fu_self_test_tear_down);

	g_test_add("/uefi-dbx/coldplug/with-device",
		   FuTestFixture,
		   &simple,
		   fu_self_test_set_up,
		   fu_uefi_dbx_test_plugin_coldplug_probed_device,
		   fu_self_test_tear_down);

	g_test_add("/uefi-dbx/coldplug/snapd/running-in-snap/supported",
		   FuTestFixture,
		   &running_in_snap,
		   fu_self_test_set_up,
		   fu_uefi_dbx_test_plugin_coldplug_probed_device,
		   fu_self_test_tear_down);
	g_test_add("/uefi-dbx/coldplug/snapd/fde-detected/supported",
		   FuTestFixture,
		   &snapd_fde_detected,
		   fu_self_test_set_up,
		   fu_uefi_dbx_test_plugin_coldplug_probed_device,
		   fu_self_test_tear_down);

	g_test_add("/uefi-dbx/coldplug/snapd/running-in-snap/not-supported",
		   FuTestFixture,
		   &running_in_snap_no_support,
		   fu_self_test_set_up,
		   fu_uefi_dbx_test_plugin_coldplug_probed_device,
		   fu_self_test_tear_down);
	g_test_add("/uefi-dbx/coldplug/snapd/fde-detected/not-supported",
		   FuTestFixture,
		   &snapd_fde_detected_no_support,
		   fu_self_test_set_up,
		   fu_uefi_dbx_test_plugin_coldplug_probed_device,
		   fu_self_test_tear_down);

	g_test_add("/uefi-dbx/coldplug/snapd/running-in-snap/supported-failure",
		   FuTestFixture,
		   &running_in_snap_bad_startup,
		   fu_self_test_set_up,
		   fu_uefi_dbx_test_plugin_coldplug_probed_device,
		   fu_self_test_tear_down);
	g_test_add("/uefi-dbx/coldplug/snapd/fde-detected/supported-failure",
		   FuTestFixture,
		   &snapd_fde_detected_bad_startup,
		   fu_self_test_set_up,
		   fu_uefi_dbx_test_plugin_coldplug_probed_device,
		   fu_self_test_tear_down);
	g_test_add("/uefi-dbx/update/snapd/running-in-snap/success",
		   FuTestFixture,
		   &running_in_snap_update,
		   fu_self_test_set_up,
		   fu_uefi_dbx_test_plugin_update,
		   fu_self_test_tear_down);
	g_test_add("/uefi-dbx/update/snapd/fde-detected/success",
		   FuTestFixture,
		   &snapd_fde_detected_update,
		   fu_self_test_set_up,
		   fu_uefi_dbx_test_plugin_update,
		   fu_self_test_tear_down);

	g_test_add("/uefi-dbx/update/snapd/running-in-snap/failed-prepare",
		   FuTestFixture,
		   &running_in_snap_update_failed_prepare,
		   fu_self_test_set_up,
		   fu_uefi_dbx_test_plugin_failed_update,
		   fu_self_test_tear_down);
	g_test_add("/uefi-dbx/update/snapd/fde-detected/failed-prepare",
		   FuTestFixture,
		   &snapd_fde_detected_update_failed_prepare,
		   fu_self_test_set_up,
		   fu_uefi_dbx_test_plugin_failed_update,
		   fu_self_test_tear_down);

	g_test_add("/uefi-dbx/update/snapd/running-in-snap/failed-cleanup",
		   FuTestFixture,
		   &running_in_snap_update_failed_cleanup,
		   fu_self_test_set_up,
		   fu_uefi_dbx_test_plugin_failed_update,
		   fu_self_test_tear_down);
	g_test_add("/uefi-dbx/update/snapd/fde-detected/failed-cleanup",
		   FuTestFixture,
		   &snapd_fde_detected_update_failed_cleanup,
		   fu_self_test_set_up,
		   fu_uefi_dbx_test_plugin_failed_update,
		   fu_self_test_tear_down);

	return g_test_run();
}
