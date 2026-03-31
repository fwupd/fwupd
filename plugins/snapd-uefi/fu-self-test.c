/*
 * Copyright 2024 Maciej Borzecki <maciej.borzecki@canonical.com>
 * Copyright 2025 Simon Johnsson <simon.johnsson@canonical.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include <curl/curl.h>
#include <curl/easy.h>

#include "fwupd-error.h"

#include "fu-context-private.h"
#include "fu-plugin-private.h"
#include "fu-snapd-uefi-plugin.h"
#include "fu-temporary-directory.h"
#include "fu-uefi-device-private.h"

typedef struct {
	gboolean snapd_supported;	  /* expecting snapd to be present */
	gboolean snapd_fde_detected;	  /* mock environment as if FDE was detected */
	gboolean in_snap;		  /* mock environment as if executing in a snap */
	const gchar *mock_snapd_scenario; /* name of scenario, see snapd.py */
	const gchar *expected_error_message;
} FuTestCase;

typedef struct {
	FuContext *ctx;
	FuTemporaryDirectory *tmpdir;

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

	g_debug("rsp:%s", buf->data);

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
	g_autofree gchar *testfwdir = NULL;

	fixture->ctx = fu_context_new();

	fixture->tmpdir = fu_temporary_directory_new("snapd-uefi-test", &error);
	g_assert_no_error(error);

	g_debug("test case: in-snap: %d fde-detected: %d snapd-supported: %d scenario: %s",
		tc->in_snap,
		tc->snapd_fde_detected,
		tc->snapd_supported,
		tc->mock_snapd_scenario);

	if (tc->in_snap) {
		/* snapd-uefi expects to be running in a snap */
		(void)g_setenv("SNAP", "fwupd", TRUE);
	}

	if (tc->snapd_fde_detected)
		fu_context_add_flag(fixture->ctx, FU_CONTEXT_FLAG_FDE_SNAPD);

	if (fu_self_test_mock_snapd_init(fixture)) {
		fixture->mock_snapd_available = TRUE;

		g_debug("set SNAP environment variable for snapd integration tests");

		if (tc->mock_snapd_scenario != NULL) {
			ret = fu_self_test_mock_snapd_setup_scenario(fixture,
								     tc->mock_snapd_scenario);
			g_assert_true(ret);
		}
	}

	testfwdir = fu_temporary_directory_build(fixture->tmpdir, "fw", NULL);
	fu_context_set_path(fixture->ctx, FU_PATH_KIND_SYSFSDIR_FW, testfwdir);

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
	/* nocheck:blocked
	 * test should always run in a snap */
	g_unsetenv("SNAP");

	if (fixture->mock_snapd_available)
		fu_self_test_mock_snapd_reset(fixture);

	if (fixture->mock_snapd_curl)
		curl_easy_cleanup(fixture->mock_snapd_curl);

	if (fixture->mock_curl_hdrs)
		curl_slist_free_all(fixture->mock_curl_hdrs);

	g_object_unref(fixture->ctx);
	g_object_unref(fixture->tmpdir);
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
fu_test_mock_dbx_efivars(FuTestFixture *fixture, FuEfivars *efivars, GError **error)
{
	const gchar *fw_path = NULL;
	g_autofree gchar *efivars_path = NULL;
	g_autofree gchar *mock_kek_path =
	    g_test_build_filename(G_TEST_DIST,
				  "tests/KEK-8be4df61-93ca-11d2-aa0d-00e098032b8c",
				  NULL);
	g_autofree gchar *mock_dbx_path =
	    g_test_build_filename(G_TEST_DIST,
				  "tests/dbx-d719b2cb-3d3a-4596-a3bc-dad00e67656f",
				  NULL);
	/* mocked by the test suite */
	fw_path = fu_context_get_path(fixture->ctx, FU_PATH_KIND_SYSFSDIR_FW, error);
	if (fw_path == NULL)
		return FALSE;

	g_debug("mocked fw path: %s", fw_path);

	efivars_path = g_build_filename(fw_path, "efi", "efivars", NULL);

	if (!fu_path_mkdir(efivars_path, error))
		return FALSE;

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

static FuPlugin *
fu_self_test_snapd_uefi_plugin_new_with_test_defaults(FuContext *ctx)
{
	FuPlugin *plugin = fu_plugin_new_from_gtype(FU_TYPE_SNAPD_UEFI_PLUGIN, ctx);
	fu_plugin_set_config_default(plugin, "KeyDBOverride", "DBX");
	return plugin;
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
	g_autoptr(FuPlugin) plugin = fu_self_test_snapd_uefi_plugin_new_with_test_defaults(ctx);
	g_autoptr(FuUefiDevice) uefi_device = NULL;
	g_autoptr(GPtrArray) devices = NULL;

	/* progress */
	fu_progress_add_flag(progress, FU_PROGRESS_FLAG_NO_PROFILE);
	fu_progress_add_step(progress, FWUPD_STATUS_LOADING, 33, NULL);
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_WRITE, 33, NULL);
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_VERIFY, 33, NULL);

	if (!fixture->mock_snapd_available) {
		g_test_skip("mock snapd not available");
		return;
	}

	if (!fu_test_mock_dbx_efivars(fixture, efivars, &error) &&
	    g_error_matches(error, G_FILE_ERROR, G_FILE_ERROR_NOENT)) {
		g_test_skip("test assets unavailable");
		return;
	}
	g_assert_no_error(error);

	ret = fu_plugin_runner_startup(plugin, fu_progress_get_child(progress), &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	fu_progress_step_done(progress);

	uefi_device = g_object_new(FU_TYPE_UEFI_DEVICE, "context", ctx, NULL);
	fu_uefi_device_set_guid(FU_UEFI_DEVICE(uefi_device), FU_EFIVARS_GUID_EFI_GLOBAL);
	fu_uefi_device_set_name(FU_UEFI_DEVICE(uefi_device), "KEK");
	fu_plugin_runner_device_register(plugin, FU_DEVICE(uefi_device));

	firmware = fu_test_mock_dbx_update_firmware();
	ret = fu_plugin_runner_composite_peek_firmware(plugin,
						       FU_DEVICE(uefi_device),
						       firmware,
						       fu_progress_get_child(progress),
						       /* skip verification of ESP binaries*/
						       FWUPD_INSTALL_FLAG_FORCE,
						       &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	fu_progress_step_done(progress);

	/* engine invokes cleanup */
	devices = g_ptr_array_new();
	g_ptr_array_add(devices, uefi_device);

	ret = fu_plugin_runner_composite_cleanup(plugin, devices, &error);
	g_assert_true(ret);
	g_assert_no_error(error);

	if (tc->snapd_supported) {
		fu_self_test_mock_snapd_assert_calls(fixture,
						     (FuTestSnapdCalls){
							 .startup = 1,
							 .prepare = 1,
							 .cleanup = 1,
						     });
	} else {
		/* nothing beyond the initial probe */
		fu_self_test_mock_snapd_assert_calls(fixture,
						     (FuTestSnapdCalls){
							 .startup = 1,
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
	g_autoptr(FuPlugin) plugin = fu_self_test_snapd_uefi_plugin_new_with_test_defaults(ctx);
	g_autoptr(FuUefiDevice) uefi_device = NULL;
	g_autoptr(GPtrArray) devices = NULL;

	if (!tc->snapd_supported) {
		g_test_skip("only supports snapd integration variant");
		return;
	}

	if (!fixture->mock_snapd_available) {
		g_test_skip("mock snapd not available");
		return;
	}

	if (!fu_test_mock_dbx_efivars(fixture, efivars, &error) &&
	    g_error_matches(error, G_FILE_ERROR, G_FILE_ERROR_NOENT)) {
		g_test_skip("test assets unavailable");
		return;
	}
	g_assert_no_error(error);

	ret = fu_plugin_runner_startup(plugin, progress, &error);
	g_assert_no_error(error);
	g_assert_true(ret);

	uefi_device = g_object_new(FU_TYPE_UEFI_DEVICE, "context", ctx, NULL);
	fu_uefi_device_set_guid(FU_UEFI_DEVICE(uefi_device), FU_EFIVARS_GUID_EFI_GLOBAL);
	fu_uefi_device_set_name(FU_UEFI_DEVICE(uefi_device), "KEK");
	fu_plugin_runner_device_register(plugin, FU_DEVICE(uefi_device));

	firmware = fu_test_mock_dbx_update_firmware();
	ret = fu_plugin_runner_composite_peek_firmware(plugin,
						       FU_DEVICE(uefi_device),
						       firmware,
						       progress_write,
						       /* skip verification of ESP binaries*/
						       FWUPD_INSTALL_FLAG_FORCE,
						       &error);
	if (g_str_equal(tc->mock_snapd_scenario, "failed-prepare") ||
	    g_str_equal(tc->mock_snapd_scenario, "failed-prepare-invalid-json")) {
		g_assert_error(error, FWUPD_ERROR, FWUPD_ERROR_INTERNAL);
		g_assert_nonnull(tc->expected_error_message);
		g_assert_cmpstr(error->message, ==, tc->expected_error_message);
		g_clear_error(&error);
	} else {
		g_assert_no_error(error);
		g_assert_true(ret);
	}

	/* engine invokes cleanup */
	devices = g_ptr_array_new();
	g_ptr_array_add(devices, uefi_device);

	ret = fu_plugin_runner_composite_cleanup(plugin, devices, &error);
	if (g_str_equal(tc->mock_snapd_scenario, "failed-cleanup")) {
		g_assert_error(error, FWUPD_ERROR, FWUPD_ERROR_INTERNAL);
		g_assert_nonnull(tc->expected_error_message);
		g_assert_cmpstr(error->message, ==, tc->expected_error_message);
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
	g_autoptr(FuPlugin) plugin = fu_self_test_snapd_uefi_plugin_new_with_test_defaults(ctx);
	g_autoptr(FuUefiDevice) uefi_device = NULL;

	if (!fixture->mock_snapd_available) {
		g_test_skip("mock snapd not available");
		return;
	}

	if (!fu_test_mock_dbx_efivars(fixture, efivars, &error) &&
	    g_error_matches(error, G_FILE_ERROR, G_FILE_ERROR_NOENT)) {
		g_test_skip("test assets unavailable");
		return;
	}
	g_assert_no_error(error);

	ret = fu_plugin_runner_startup(plugin, progress, &error);
	g_assert_no_error(error);
	g_assert_true(ret);

	uefi_device = g_object_new(FU_TYPE_UEFI_DEVICE, "context", ctx, NULL);
	fu_uefi_device_set_guid(FU_UEFI_DEVICE(uefi_device), FU_EFIVARS_GUID_EFI_GLOBAL);
	fu_uefi_device_set_name(FU_UEFI_DEVICE(uefi_device), "KEK");
	fu_plugin_runner_device_register(plugin, FU_DEVICE(uefi_device));

	ret = fu_device_has_inhibit(FU_DEVICE(uefi_device), "no-snapd");
	if (tc->snapd_supported && g_str_equal(tc->mock_snapd_scenario, "failed-startup")) {
		/* startup failed for whatever reason, device updates are inhibited */
		g_assert_true(ret);
	} else
		g_assert_false(ret);

	fu_self_test_mock_snapd_assert_calls(fixture,
					     (FuTestSnapdCalls){
						 .startup = 1,
					     });
}

static void
fu_uefi_dbx_test_plugin_startup(FuTestFixture *fixture, gconstpointer user_data)
{
	FuTestCase *tc = (FuTestCase *)user_data;
	gboolean ret;
	FuContext *ctx = fixture->ctx;
	g_autoptr(FuProgress) progress = fu_progress_new(G_STRLOC);
	g_autoptr(GError) error = NULL;
	g_autoptr(FuPlugin) plugin = fu_self_test_snapd_uefi_plugin_new_with_test_defaults(ctx);

	if (!fixture->mock_snapd_available) {
		g_test_skip("mock snapd not available");
		return;
	}

	ret = fu_context_load_quirks(ctx,
				     FU_QUIRKS_LOAD_FLAG_NO_CACHE | FU_QUIRKS_LOAD_FLAG_NO_VERIFY,
				     &error);
	g_assert_no_error(error);
	g_assert_true(ret);

	ret = fu_plugin_runner_startup(plugin, progress, &error);
	if (tc->snapd_fde_detected || tc->in_snap) {
		/* whenever inside a snap or FDE is in use, the plugin is started */
		g_assert_no_error(error);
		g_assert_true(ret);
		fu_self_test_mock_snapd_assert_calls(fixture,
						     (FuTestSnapdCalls){
							 .startup = 1,
						     });
	} else {
		g_assert_error(error, FWUPD_ERROR, FWUPD_ERROR_NOT_SUPPORTED);
		g_assert_false(ret);
		fu_self_test_mock_snapd_assert_calls(fixture,
						     (FuTestSnapdCalls){
							 .startup = 0, /* no calls */
						     });
	}
}

int
main(int argc, char **argv)
{
	FuTestCase simple = {
	    .snapd_supported = TRUE,
	};
	/* test variants with snapd, see tests/snapd.py for what a specific scenario
	 * supports */
	FuTestCase running_in_snap = {
	    .in_snap = TRUE,
	    .mock_snapd_scenario = "happy-startup",
	    .snapd_supported = TRUE,
	};
	FuTestCase snapd_fde_detected = {
	    .mock_snapd_scenario = "happy-startup",
	    .snapd_fde_detected = TRUE,
	    .snapd_supported = TRUE,
	};
	FuTestCase running_in_snap_snapd_fde_detected = {
	    .in_snap = TRUE,
	    .mock_snapd_scenario = "happy-startup",
	    .snapd_fde_detected = TRUE,
	    .snapd_supported = TRUE,
	};
	FuTestCase running_in_snap_no_support = {
	    .in_snap = TRUE,
	    .mock_snapd_scenario = "not-supported",
	    .snapd_supported = FALSE,
	};
	FuTestCase snapd_fde_detected_no_support = {
	    .mock_snapd_scenario = "not-supported",
	    .snapd_fde_detected = TRUE,
	    .snapd_supported = FALSE,
	};
	FuTestCase running_in_snap_bad_startup = {
	    .in_snap = TRUE,
	    .mock_snapd_scenario = "failed-startup",
	    .snapd_supported = TRUE,
	};
	FuTestCase snapd_fde_detected_bad_startup = {
	    .mock_snapd_scenario = "failed-startup",
	    .snapd_fde_detected = TRUE,
	    .snapd_supported = TRUE,
	};
	FuTestCase running_in_snap_update = {
	    .in_snap = TRUE,
	    .mock_snapd_scenario = "happy-update",
	    .snapd_supported = TRUE,
	};
	FuTestCase snapd_fde_detected_update = {
	    .mock_snapd_scenario = "happy-update",
	    .snapd_fde_detected = TRUE,
	    .snapd_supported = TRUE,
	};
	FuTestCase running_in_snap_update_failed_prepare = {
	    .in_snap = TRUE,
	    .mock_snapd_scenario = "failed-prepare",
	    .expected_error_message =
		"failed to notify snapd of prepare: snapd request failed with status 400: "
		"cannot reseal keys in prepare",
	    .snapd_supported = TRUE,
	};
	FuTestCase snapd_fde_detected_update_failed_prepare = {
	    .expected_error_message =
		"failed to notify snapd of prepare: snapd request failed with status 400: "
		"cannot reseal keys in prepare",
	    .mock_snapd_scenario = "failed-prepare",
	    .snapd_fde_detected = TRUE,
	    .snapd_supported = TRUE,
	};
	FuTestCase snapd_fde_detected_update_failed_prepare_invalid_json = {
	    .expected_error_message =
		"failed to notify snapd of prepare: snapd request failed with status 400",
	    .mock_snapd_scenario = "failed-prepare-invalid-json",
	    .snapd_fde_detected = TRUE,
	    .snapd_supported = TRUE,
	};
	FuTestCase running_in_snap_update_failed_cleanup = {
	    .expected_error_message = "failed to composite_cleanup using snap: failed to notify "
				      "snapd of cleanup: snapd request failed with status 500: "
				      "cannot reseal keys in cleanup",
	    .in_snap = TRUE,
	    .mock_snapd_scenario = "failed-cleanup",
	    .snapd_supported = TRUE,
	};
	FuTestCase snapd_fde_detected_update_failed_cleanup = {
	    .expected_error_message = "failed to composite_cleanup using snap: failed to notify "
				      "snapd of cleanup: snapd request failed with status 500: "
				      "cannot reseal keys in cleanup",
	    .mock_snapd_scenario = "failed-cleanup",
	    .snapd_fde_detected = TRUE,
	    .snapd_supported = TRUE,
	};

	(void)g_setenv("G_TEST_SRCDIR", SRCDIR, FALSE);
	g_test_init(&argc, &argv, NULL);
	(void)g_setenv("FWUPD_SNAPD_SNAP_SOCKET", "/tmp/mock-snapd-test.sock", TRUE);

	g_test_add("/uefi-dbx/startup",
		   FuTestFixture,
		   &simple,
		   fu_self_test_set_up,
		   fu_uefi_dbx_test_plugin_startup,
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
	g_test_add("/uefi-dbx/update/snapd/running-in-snap/no_support",
		   FuTestFixture,
		   &running_in_snap_no_support,
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

	g_test_add("/uefi-dbx/update/snapd/fde-detected/failed-prepare-invalid-json",
		   FuTestFixture,
		   &snapd_fde_detected_update_failed_prepare_invalid_json,
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
