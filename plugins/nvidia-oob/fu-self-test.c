/*
 * Copyright 2026 NVIDIA Corporation
 * Author: Vishnu Raghav <vraghav@nvidia.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include <glib/gstdio.h>

#include "fu-config-private.h"
#include "fu-context-private.h"
#include "fu-device-private.h"
#include "fu-nvidia-oob-device.h"
#include "fu-nvidia-oob-plugin.h"
#include "fu-plugin-private.h"

/* CHID of "NVIDIA" + "GB300 DGX Station", which nvidia-oob.quirk flags */
#define FU_NVIDIA_OOB_TEST_CHID "c3cf31f2-8b8a-531d-bcf4-636a0aaa9ffc"

typedef struct {
	GSubprocess *mock;
	gchar *tmpdir;
	FuPlugin *plugin;
} FuTest;

static FuContext *
fu_nvidia_oob_test_context_new(const gchar *tmpdir, gboolean is_station)
{
	gboolean ret;
	g_autoptr(FuContext) ctx = fu_context_new();
	g_autoptr(FuProgress) progress = fu_progress_new(G_STRLOC);
	g_autoptr(GError) error = NULL;

	fu_context_add_flag(ctx, FU_CONTEXT_FLAG_NO_CACHE);
	fu_context_set_path(ctx, FU_PATH_KIND_DATADIR_QUIRKS, g_test_get_dir(G_TEST_DIST));
	fu_context_set_path(ctx, FU_PATH_KIND_SYSFSDIR_FW, tmpdir);
	fu_context_set_path(ctx, FU_PATH_KIND_SYSCONFDIR_PKG, tmpdir);
	fu_config_set_basename(fu_context_get_config(ctx), "nvidia-oob-fwupd.conf");
	if (is_station)
		fu_hwids_add_guid(fu_context_get_hwids(ctx), FU_NVIDIA_OOB_TEST_CHID);
	/* a brand other than NVIDIA shows the vendor IDs come from SMBIOS */
	fu_hwids_add_value(fu_context_get_hwids(ctx), FU_HWIDS_KEY_MANUFACTURER, "Contoso");
	ret = fu_context_load(ctx, progress, FU_CONTEXT_LOAD_FLAG_NONE, &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	return g_steal_pointer(&ctx);
}

static FuDevice *
fu_nvidia_oob_test_find_device(GPtrArray *devices, const gchar *backend_id)
{
	for (guint i = 0; i < devices->len; i++) {
		FuDevice *device = g_ptr_array_index(devices, i);
		if (g_strcmp0(fu_device_get_backend_id(device), backend_id) == 0)
			return device;
	}
	return NULL;
}

static gboolean
fu_nvidia_oob_test_has_rule(FuPlugin *plugin, FuPluginRule rule, const gchar *name)
{
	GPtrArray *rules = fu_plugin_get_rules(plugin, rule);
	for (guint i = 0; rules != NULL && i < rules->len; i++) {
		if (g_strcmp0(g_ptr_array_index(rules, i), name) == 0)
			return TRUE;
	}
	return FALSE;
}

static void
fu_nvidia_oob_not_station_func(gconstpointer user_data)
{
	FuTest *self = (FuTest *)user_data;
	gboolean ret;
	g_autoptr(FuContext) ctx = NULL;
	g_autoptr(FuPlugin) plugin = NULL;
	g_autoptr(FuProgress) progress = fu_progress_new(G_STRLOC);
	g_autoptr(GError) error = NULL;

	if (self->tmpdir == NULL) {
		g_test_skip("no mock BMC");
		return;
	}

	/* without the hardware ID the plugin stays out of the way of redfish */
	ctx = fu_nvidia_oob_test_context_new(self->tmpdir, FALSE);
	plugin = fu_plugin_new_from_gtype(fu_nvidia_oob_plugin_get_type(), ctx);
	ret = fu_plugin_runner_startup(plugin, progress, &error);
	g_assert_error(error, FWUPD_ERROR, FWUPD_ERROR_NOT_SUPPORTED);
	g_assert_false(ret);
	g_assert_false(fu_nvidia_oob_test_has_rule(plugin, FU_PLUGIN_RULE_CONFLICTS, "redfish"));
}

static void
fu_nvidia_oob_update_func(gconstpointer user_data)
{
	FuTest *self = (FuTest *)user_data;
	FuDevice *dev_bmc;
	FuDevice *dev_gpu;
	GPtrArray *devices;
	gboolean ret;
	g_autoptr(FuFirmware) firmware = NULL;
	g_autoptr(FuFirmware) firmware_badodata = NULL;
	g_autoptr(FuFirmware) firmware_nolocation = NULL;
	g_autoptr(FuProgress) progress = fu_progress_new(G_STRLOC);
	g_autoptr(FuProgress) progress_activate = fu_progress_new(G_STRLOC);
	g_autoptr(FuProgress) progress_again = fu_progress_new(G_STRLOC);
	g_autoptr(FuProgress) progress_badodata = fu_progress_new(G_STRLOC);
	g_autoptr(FuProgress) progress_nolocation = fu_progress_new(G_STRLOC);
	g_autoptr(GBytes) blob_fw = NULL;
	g_autoptr(GBytes) blob_fw_badodata = NULL;
	g_autoptr(GBytes) blob_fw_nolocation = NULL;
	g_autoptr(GError) error = NULL;

	if (self->plugin == NULL) {
		g_test_skip("no mock BMC");
		return;
	}
	fu_progress_add_flag(progress, FU_PROGRESS_FLAG_NO_PROFILE);
	fu_progress_add_flag(progress_activate, FU_PROGRESS_FLAG_NO_PROFILE);
	fu_progress_add_flag(progress_again, FU_PROGRESS_FLAG_NO_PROFILE);
	fu_progress_add_flag(progress_badodata, FU_PROGRESS_FLAG_NO_PROFILE);
	fu_progress_add_flag(progress_nolocation, FU_PROGRESS_FLAG_NO_PROFILE);

	/* on a matching system the generic redfish plugin is disabled */
	g_assert_true(
	    fu_nvidia_oob_test_has_rule(self->plugin, FU_PLUGIN_RULE_CONFLICTS, "redfish"));
	g_assert_true(
	    fu_nvidia_oob_test_has_rule(self->plugin, FU_PLUGIN_RULE_RUN_BEFORE, "redfish"));

	devices = fu_plugin_get_devices(self->plugin);
	g_assert_cmpint(devices->len, ==, 2);
	dev_bmc = fu_nvidia_oob_test_find_device(devices, "FW_BMC_0");
	dev_gpu = fu_nvidia_oob_test_find_device(devices, "FW_GPU_0");
	g_assert_nonnull(dev_bmc);
	g_assert_nonnull(dev_gpu);

	/* every entry is named "Software Inventory", so the device is named after its
	 * inventory Id and matched by it, with no manufacturer */
	g_assert_cmpstr(fu_device_get_name(dev_bmc), ==, "FW BMC 0");
	g_assert_cmpstr(fu_device_get_version(dev_bmc), ==, "GB300-1.00");
	g_assert_true(fu_device_has_flag(dev_bmc, FWUPD_DEVICE_FLAG_UPDATABLE));
	g_assert_true(fu_device_has_protocol(dev_bmc, "org.dmtf.redfish"));
	g_assert_true(fu_device_has_instance_id(dev_bmc,
						"REDFISH\\ID_FW_BMC_0",
						FU_DEVICE_INSTANCE_FLAG_VISIBLE));
	g_assert_true(fu_device_has_private_flag(dev_bmc, FU_DEVICE_PRIVATE_FLAG_ENFORCE_REQUIRES));
	g_assert_true(fu_device_has_vendor_id(dev_bmc, "DMI:Contoso"));
	g_assert_true(fu_device_has_vendor_id(dev_bmc, "OEM:Contoso"));
	g_assert_false(fu_device_has_vendor_id(dev_bmc, "REDFISH:NVIDIA"));
	g_assert_false(fu_device_has_flag(dev_bmc, FWUPD_DEVICE_FLAG_NEEDS_ACTIVATION));

	/* the GPU's board variant comes from the PCIe function under its chassis */
	g_assert_true(
	    fu_device_has_instance_id(dev_gpu,
				      "REDFISH\\ID_FW_GPU_0&VEN_10DE&DEV_31C2&SUBSYS_10DE20E5",
				      FU_DEVICE_INSTANCE_FLAG_VISIBLE));
	g_assert_false(fu_device_has_vendor_id(dev_gpu, "PCI:0x10DE"));

	/* the mock rejects the upload unless Targets is empty, ForceUpdate is set and
	 * the apply time is Immediate; inside an install transaction the staged
	 * components are only flagged once every component has been processed */
	blob_fw = g_bytes_new_static("hello", 5);
	firmware = fu_firmware_new_from_bytes(blob_fw);
	ret = fu_plugin_runner_composite_prepare(self->plugin, devices, &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	ret = fu_plugin_runner_write_firmware(self->plugin,
					      dev_bmc,
					      firmware,
					      progress,
					      FWUPD_INSTALL_FLAG_NONE,
					      &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	g_assert_false(fu_device_has_flag(dev_gpu, FWUPD_DEVICE_FLAG_NEEDS_ACTIVATION));
	ret = fu_plugin_runner_composite_cleanup(self->plugin, devices, &error);
	g_assert_no_error(error);
	g_assert_true(ret);

	/* the BMC staged the GPU rather than the device written, and every updatable
	 * component now takes only explicit updates */
	g_assert_true(fu_device_has_flag(dev_gpu, FWUPD_DEVICE_FLAG_NEEDS_ACTIVATION));
	g_assert_false(fu_device_has_flag(dev_bmc, FWUPD_DEVICE_FLAG_NEEDS_ACTIVATION));
	g_assert_true(fu_device_has_flag(dev_bmc, FWUPD_DEVICE_FLAG_ONLY_EXPLICIT_UPDATES));
	g_assert_true(fu_device_has_flag(dev_gpu, FWUPD_DEVICE_FLAG_ONLY_EXPLICIT_UPDATES));
	g_assert_false(fu_device_has_flag(dev_bmc, FWUPD_DEVICE_FLAG_NEEDS_SHUTDOWN));

	/* every component of an archive shares one payload, so it is not sent again;
	 * the mock counts uploads, so a resend would take the next response */
	ret = fu_plugin_runner_write_firmware(self->plugin,
					      dev_gpu,
					      firmware,
					      progress_again,
					      FWUPD_INSTALL_FLAG_NONE,
					      &error);
	g_assert_no_error(error);
	g_assert_true(ret);

	/* activation finds the OEM action on BMC_0 and asks for the aux power cycle;
	 * the mock rejects any other ResetType */
	ret = fu_plugin_runner_activate(self->plugin, dev_gpu, progress_activate, &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	g_assert_false(fu_device_has_flag(dev_gpu, FWUPD_DEVICE_FLAG_NEEDS_ACTIVATION));

	/* the second upload has no Location header, so the task monitor comes from
	 * @odata.id in the response body */
	blob_fw_nolocation = g_bytes_new_static("hello-nolocation", 16);
	firmware_nolocation = fu_firmware_new_from_bytes(blob_fw_nolocation);
	ret = fu_plugin_runner_write_firmware(self->plugin,
					      dev_bmc,
					      firmware_nolocation,
					      progress_nolocation,
					      FWUPD_INSTALL_FLAG_NONE,
					      &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	g_assert_true(fu_device_has_flag(dev_gpu, FWUPD_DEVICE_FLAG_NEEDS_ACTIVATION));

	/* the third has an @odata.id that is not a string, which has to be rejected
	 * rather than leave a NULL task to poll */
	blob_fw_badodata = g_bytes_new_static("hello-badodata", 14);
	firmware_badodata = fu_firmware_new_from_bytes(blob_fw_badodata);
	ret = fu_plugin_runner_write_firmware(self->plugin,
					      dev_bmc,
					      firmware_badodata,
					      progress_badodata,
					      FWUPD_INSTALL_FLAG_NONE,
					      &error);
	g_assert_error(error, FWUPD_ERROR, FWUPD_ERROR_NOT_SUPPORTED);
	g_assert_false(ret);
}

static gboolean
fu_nvidia_oob_test_start_mock(FuTest *self, GError **error)
{
	const gchar *line;
	g_autofree gchar *conf = NULL;
	g_autofree gchar *conf_fn = NULL;
	g_autofree gchar *mock_fn = NULL;
	g_autofree gchar *session_fn = NULL;
	g_autoptr(GDataInputStream) stream = NULL;
	g_autofree gchar *port_line = NULL;

	mock_fn = g_test_build_filename(G_TEST_DIST, "tests", "nvidia-oob-mock.py", NULL);
	self->mock =
	    g_subprocess_new(G_SUBPROCESS_FLAGS_STDOUT_PIPE, error, "python3", mock_fn, NULL);
	if (self->mock == NULL)
		return FALSE;
	stream = g_data_input_stream_new(g_subprocess_get_stdout_pipe(self->mock));
	port_line = g_data_input_stream_read_line(stream, NULL, NULL, error);
	if (port_line == NULL || !g_str_has_prefix(port_line, "PORT ")) {
		if (error != NULL && *error == NULL)
			g_set_error_literal(error,
					    FWUPD_ERROR,
					    FWUPD_ERROR_NOT_FOUND,
					    "the mock BMC did not start");
		return FALSE;
	}
	line = port_line + 5;

	/* the session key the mock accepts, and the config pointing at the mock */
	self->tmpdir = g_dir_make_tmp("fwupd-nvidia-oob-XXXXXX", error);
	if (self->tmpdir == NULL)
		return FALSE;
	session_fn = g_build_filename(self->tmpdir, "session", NULL);
	if (!g_file_set_contents(session_fn, "nvidia-oob-session-key\n", -1, error))
		return FALSE;
	conf = g_strdup_printf("[nvidia_oob]\nUri=http://127.0.0.1:%s\nSessionKeyFile=%s\n",
			       line,
			       session_fn);
	conf_fn = g_build_filename(self->tmpdir, "nvidia-oob-fwupd.conf", NULL);
	if (!g_file_set_contents(conf_fn, conf, -1, error))
		return FALSE;
	if (g_chmod(conf_fn, 0640) != 0) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INTERNAL,
			    "failed to chmod %s",
			    conf_fn);
		return FALSE;
	}
	return TRUE;
}

static void
fu_nvidia_oob_test_free(FuTest *self)
{
	if (self->plugin != NULL)
		g_object_unref(self->plugin);
	if (self->mock != NULL) {
		g_subprocess_force_exit(self->mock);
		g_object_unref(self->mock);
	}
	if (self->tmpdir != NULL) {
		g_autofree gchar *conf_fn =
		    g_build_filename(self->tmpdir, "nvidia-oob-fwupd.conf", NULL);
		g_autofree gchar *session_fn = g_build_filename(self->tmpdir, "session", NULL);
		g_unlink(conf_fn);
		g_unlink(session_fn);
		g_rmdir(self->tmpdir);
		g_free(self->tmpdir);
	}
	g_free(self);
}

G_DEFINE_AUTOPTR_CLEANUP_FUNC(FuTest, fu_nvidia_oob_test_free)

int
main(int argc, char **argv)
{
	gboolean ret;
	g_autoptr(FuContext) ctx = NULL;
	g_autoptr(FuProgress) progress = fu_progress_new(G_STRLOC);
	g_autoptr(FuTest) self = g_new0(FuTest, 1);
	g_autoptr(GError) error = NULL;

	(void)g_setenv("G_TEST_SRCDIR", SRCDIR, FALSE);
	g_test_init(&argc, &argv, NULL);
	g_log_set_fatal_mask(NULL, G_LOG_LEVEL_ERROR | G_LOG_LEVEL_CRITICAL);
	(void)g_setenv("G_MESSAGES_DEBUG", "all", TRUE);

	/* without python3 there is no mock BMC, and the tests skip */
	if (!fu_nvidia_oob_test_start_mock(self, &error)) {
		g_debug("no mock BMC: %s", error->message);
	} else {
		ctx = fu_nvidia_oob_test_context_new(self->tmpdir, TRUE);
		self->plugin = fu_plugin_new_from_gtype(fu_nvidia_oob_plugin_get_type(), ctx);
		ret = fu_plugin_runner_startup(self->plugin, progress, &error);
		g_assert_no_error(error);
		g_assert_true(ret);
		ret = fu_plugin_runner_coldplug(self->plugin, progress, &error);
		g_assert_no_error(error);
		g_assert_true(ret);
	}

	g_test_add_data_func("/nvidia-oob/not-station", self, fu_nvidia_oob_not_station_func);
	g_test_add_data_func("/nvidia-oob/update", self, fu_nvidia_oob_update_func);
	return g_test_run();
}
