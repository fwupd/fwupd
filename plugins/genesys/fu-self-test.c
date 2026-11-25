/*
 * Copyright 2026 Genesys Logic, Inc.
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include "fu-cfi-device.h"
#include "fu-context-private.h"
#include "fu-device-private.h"
#include "fu-genesys-hubhid-device.h"
#include "fu-genesys-plugin.h"
#include "fu-genesys-usbhub-device.h"
#include "fu-genesys-usbhub-firmware.h"
#include "fu-plugin-private.h"

/*
 * Test vectors use literal hardware VID/PIDs, offsets, and version pairs.
 * nocheck:magic-inlines=240
 */

static gchar *
fu_genesys_test_quirks_dir(void)
{
	g_autofree gchar *installed = g_test_build_filename(G_TEST_DIST, "tests", "quirks.d", NULL);
	if (g_file_test(installed, G_FILE_TEST_IS_DIR))
		return g_steal_pointer(&installed);
	return g_test_build_filename(G_TEST_DIST, ".", NULL);
}

static FuContext *
fu_genesys_test_context_new(void)
{
	g_autofree gchar *quirks_dir = fu_genesys_test_quirks_dir();
	g_autoptr(FuContext) ctx = fu_context_new_full(FU_CONTEXT_FLAG_NO_CACHE);
	g_autoptr(FuProgress) progress = fu_progress_new(G_STRLOC);
	g_autoptr(GError) error = NULL;
	gboolean ret;

	fu_context_add_quirk_key(ctx, "GenesysUsbhubHidProxyId");
	fu_context_add_quirk_key(ctx, "GenesysParentHubPid");
	fu_context_add_quirk_key(ctx, "GenesysRootHubPid");
	fu_context_add_quirk_key(ctx, "GenesysUsbhubSwitchRequest");
	fu_context_add_quirk_key(ctx, "GenesysUsbhubReadRequest");
	fu_context_add_quirk_key(ctx, "GenesysUsbhubWriteRequest");
	fu_context_add_quirk_key(ctx, "GenesysSupportDualBank");
	fu_context_add_quirk_key(ctx, "GenesysSupportCodeSize");
	fu_context_add_quirk_key(ctx, "GenesysHubBank1Address");
	fu_context_add_quirk_key(ctx, "GenesysHubBank2Address");
	fu_context_add_quirk_key(ctx, "GenesysHubBankCapacity");
	fu_context_add_quirk_key(ctx, "GenesysDevBank1Address");
	fu_context_add_quirk_key(ctx, "GenesysDevBank2Address");
	fu_context_add_quirk_key(ctx, "GenesysDevBankCapacity");
	fu_context_add_quirk_key(ctx, "GenesysPdBank1Address");
	fu_context_add_quirk_key(ctx, "GenesysPdBank2Address");
	fu_context_add_quirk_key(ctx, "GenesysPdBankCapacity");
	fu_context_add_quirk_key(ctx, "GenesysCodesignBank1Address");
	fu_context_add_quirk_key(ctx, "GenesysCodesignBank2Address");
	fu_context_add_quirk_key(ctx, "GenesysCodesignBankCapacity");
	fu_context_add_quirk_key(ctx, "GenesysFwDataMaxCount");
	fu_context_add_quirk_key(ctx, "GenesysScalerCfiFlashId");
	fu_context_add_quirk_key(ctx, "GenesysScalerGpioOutputRegister");
	fu_context_add_quirk_key(ctx, "GenesysScalerGpioEnableRegister");
	fu_context_add_quirk_key(ctx, "GenesysScalerGpioValue");
	fu_context_set_path(ctx, FU_PATH_KIND_DATADIR_QUIRKS, quirks_dir);
	ret = fu_context_load(ctx, progress, FU_CONTEXT_LOAD_FLAG_NONE, &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	return g_steal_pointer(&ctx);
}

static FuDevice *
fu_genesys_test_hub_new(FuContext *ctx, const gchar *instance_id, guint16 pid)
{
	FuDevice *device = g_object_new(FU_TYPE_GENESYS_USBHUB_DEVICE, NULL);
	fu_device_set_context(device, ctx);
	fu_device_add_instance_id(device, instance_id);
	fu_device_set_vid(device, 0x17EF);
	fu_device_set_pid(device, pid);
	fu_device_set_physical_id(device, "1-4");
	return device;
}

static FuDevice *
fu_genesys_test_parent_new(guint16 vid, guint16 pid)
{
	FuDevice *device = g_object_new(FU_TYPE_DEVICE, NULL);
	fu_device_set_vid(device, vid);
	fu_device_set_pid(device, pid);
	return device;
}

/* minimal backend answering parent queries so backend topology walks can be tested offline */
#define FU_TYPE_GENESYS_TEST_BACKEND (fu_genesys_test_backend_get_type())
G_DECLARE_FINAL_TYPE(FuGenesysTestBackend,
		     fu_genesys_test_backend,
		     FU,
		     GENESYS_TEST_BACKEND,
		     FuBackend)

struct _FuGenesysTestBackend {
	FuBackend parent_instance;
	FuDevice *parent;
};

G_DEFINE_TYPE(FuGenesysTestBackend, fu_genesys_test_backend, FU_TYPE_BACKEND)

static FuDevice *
fu_genesys_test_backend_get_device_parent(FuBackend *backend,
					  FuDevice *device,
					  const gchar *subsystem,
					  GError **error)
{
	FuGenesysTestBackend *self = FU_GENESYS_TEST_BACKEND(backend);

	if (self->parent == NULL) {
		g_set_error_literal(error, FWUPD_ERROR, FWUPD_ERROR_NOT_FOUND, "no parent");
		return NULL;
	}
	return g_object_ref(self->parent);
}

static void
fu_genesys_test_backend_class_init(FuGenesysTestBackendClass *klass)
{
	FuBackendClass *backend_class = FU_BACKEND_CLASS(klass);
	backend_class->get_device_parent = fu_genesys_test_backend_get_device_parent;
}

static void
fu_genesys_test_backend_init(FuGenesysTestBackend *self)
{
}

static void
fu_genesys_test_set_backend_parent(FuContext *ctx, FuDevice *device, FuDevice *parent)
{
	g_autoptr(FuGenesysTestBackend) backend = g_object_new(FU_TYPE_GENESYS_TEST_BACKEND,
							       "name",
							       "genesys-test",
							       "context",
							       ctx,
							       NULL);
	if (parent != NULL) {
		g_autoptr(FuDevice) usb_parent =
		    fu_genesys_test_parent_new(fu_device_get_vid(parent),
					       fu_device_get_pid(parent));
		if (fu_device_get_physical_id(parent) != NULL)
			fu_device_set_physical_id(usb_parent, fu_device_get_physical_id(parent));
		/* propagate parent's backend so multi-hop backend walks work */
		if (fu_device_get_backend(parent) != NULL)
			fu_device_set_backend(usb_parent, fu_device_get_backend(parent));
		backend->parent = usb_parent;
		g_object_set_data_full(G_OBJECT(backend),
				       "genesys-test-usb-parent",
				       g_steal_pointer(&usb_parent),
				       g_object_unref);
	}
	fu_device_set_backend(device, FU_BACKEND(backend));
	g_object_set_data_full(G_OBJECT(device),
			       "genesys-test-backend",
			       g_steal_pointer(&backend),
			       g_object_unref);
}

typedef struct {
	guint n_enter;
	guint n_reset;
	guint n_other;
	gboolean allow_reset;
} FuGenesysTestIspSpy;

static gboolean
fu_genesys_test_isp_spy_cb(FuUsbDirection direction,
			   guint8 request,
			   guint16 value,
			   guint8 *data,
			   gsize length,
			   gpointer user_data,
			   GError **error)
{
	FuGenesysTestIspSpy *spy = user_data;

	if (direction == FU_USB_DIRECTION_HOST_TO_DEVICE && value == 0x0001) {
		spy->n_enter++;
	} else if (direction == FU_USB_DIRECTION_HOST_TO_DEVICE && value == 0x0003) {
		spy->n_reset++;
		if (spy->allow_reset)
			return TRUE;
	} else {
		spy->n_other++;
	}
	g_set_error_literal(error, FWUPD_ERROR, FWUPD_ERROR_READ, "spy-stop");
	return FALSE;
}

/* nocheck:name */
static guint64
fu_genesys_test_hub_pid_of(FuDevice *device, const gchar *key)
{
	g_autofree gchar *str = fu_device_to_string(device);
	g_autofree gchar *pattern = g_strdup_printf("%s:\\s+0x([0-9a-f]+)", key);
	g_autoptr(GMatchInfo) match = NULL;
	g_autoptr(GRegex) re = g_regex_new(pattern, 0, 0, NULL);
	guint64 pid = 0;

	if (!g_regex_match(re, str, 0, &match))
		return 0;
	{
		g_autofree gchar *pid_str = g_match_info_fetch(match, 1);
		g_autoptr(GError) error = NULL;
		g_assert_true(
		    fu_strtoull(pid_str, &pid, 0, G_MAXUINT16, FU_INTEGER_BASE_16, &error));
		g_assert_no_error(error);
	}
	return pid;
}

/* nocheck:name */
static gchar *
fu_genesys_test_hid_proxy_id_of(FuDevice *device)
{
	g_autofree gchar *str = fu_device_to_string(device);
	g_autoptr(GMatchInfo) match = NULL;
	g_autoptr(GRegex) re = g_regex_new("HidProxyId:\\s+(\\S+)", 0, 0, NULL);

	if (!g_regex_match(re, str, 0, &match))
		return NULL;
	return g_match_info_fetch(match, 1);
}

static void
fu_genesys_test_plugin_count_cb(FuPlugin *plugin, FuDevice *device, gpointer user_data)
{
	guint *count = (guint *)user_data;
	(*count)++;
}

static void
fu_genesys_test_plugin_add_device(FuPlugin *plugin, FuDevice *device)
{
	fu_plugin_add_device(plugin, device);
	fu_plugin_runner_device_added(plugin, device);
}

static void
fu_genesys_usbhub_device_p34_quirk_func(void)
{
	g_autoptr(FuContext) ctx = fu_genesys_test_context_new();
	struct {
		const gchar *instance_id;
		guint16 pid;
		const gchar *hid_proxy_id;
		guint64 parent_pid;
		guint64 root_pid;
		gboolean proxy_required;
		gboolean strict_project;
		gboolean enforce_requires;
		gboolean dual_image;
		guint remove_delay;
	} hubs[] = {
	    {"USB\\VID_17EF&PID_1121",
	     0x1121,
	     "USB\\VID_17EF&PID_F00F",
	     0,
	     0,
	     TRUE,
	     TRUE,
	     FALSE,
	     TRUE,
	     40000},
	    {"USB\\VID_17EF&PID_1122",
	     0x1122,
	     "USB\\VID_17EF&PID_4C41",
	     0x1121,
	     0,
	     TRUE,
	     TRUE,
	     TRUE,
	     TRUE,
	     40000},
	    {"USB\\VID_17EF&PID_1123",
	     0x1123,
	     "USB\\VID_17EF&PID_4C42",
	     0x1122,
	     0x1121,
	     TRUE,
	     TRUE,
	     TRUE,
	     TRUE,
	     40000},
	    {"USB\\VID_17EF&PID_1159", 0x1159, NULL, 0, 0, FALSE, FALSE, FALSE, FALSE, 0},
	};

	for (guint i = 0; i < G_N_ELEMENTS(hubs); i++) {
		g_autoptr(FuDevice) hub =
		    fu_genesys_test_hub_new(ctx, hubs[i].instance_id, hubs[i].pid);
		g_autofree gchar *hid_id = fu_genesys_test_hid_proxy_id_of(hub);

		g_assert_cmpstr(hid_id, ==, hubs[i].hid_proxy_id);
		g_assert_cmpuint(fu_genesys_test_hub_pid_of(hub, "ParentHubPid"),
				 ==,
				 hubs[i].parent_pid);
		g_assert_cmpuint(fu_genesys_test_hub_pid_of(hub, "RootHubPid"),
				 ==,
				 hubs[i].root_pid);
		g_assert_true(
		    fu_device_has_private_flag(hub, FU_GENESYS_USBHUB_FLAG_PROXY_REQUIRED) ==
		    hubs[i].proxy_required);
		g_assert_true(
		    fu_device_has_private_flag(hub, FU_GENESYS_USBHUB_FLAG_STRICT_PROJECT_CHECK) ==
		    hubs[i].strict_project);
		g_assert_true(
		    fu_device_has_private_flag(hub, FU_DEVICE_PRIVATE_FLAG_ENFORCE_REQUIRES) ==
		    hubs[i].enforce_requires);
		g_assert_true(fu_device_has_flag(hub, FWUPD_DEVICE_FLAG_DUAL_IMAGE) ==
			      hubs[i].dual_image);
		if (hubs[i].remove_delay != 0)
			g_assert_cmpuint(fu_device_get_remove_delay(hub), ==, hubs[i].remove_delay);
	}
}

static void
fu_genesys_usbhub_device_p34_proxy_required_fail_closed_func(void)
{
	g_autoptr(FuContext) ctx = fu_genesys_test_context_new();
	g_autoptr(FuDevice) hub = fu_genesys_test_hub_new(ctx, "USB\\VID_17EF&PID_1121", 0x1121);
	g_autoptr(FuDevice) fake_proxy = g_object_new(FU_TYPE_GENESYS_HUBHID_DEVICE, NULL);
	FuDeviceClass *klass = FU_DEVICE_GET_CLASS(hub);
	FuGenesysTestIspSpy spy = {0};
	g_autoptr(FuProgress) progress = fu_progress_new(G_STRLOC);
	g_autoptr(FuFirmware) firmware = fu_firmware_new();
	g_autoptr(GError) error = NULL;

	fu_genesys_usbhub_device_set_ctrl_hook(FU_GENESYS_USBHUB_DEVICE(hub),
					       fu_genesys_test_isp_spy_cb,
					       &spy);

	/* no proxy bound: every update entry fails closed before ISP_ENTER */
	g_assert_false(klass->detach(hub, progress, &error));
	g_assert_error(error, FWUPD_ERROR, FWUPD_ERROR_NOT_SUPPORTED);
	g_clear_error(&error);
	g_assert_false(
	    klass->prepare(hub,
			   progress,
			   FWUPD_INSTALL_FLAG_IGNORE_REQUIREMENTS | FWUPD_INSTALL_FLAG_FORCE,
			   &error));
	g_assert_error(error, FWUPD_ERROR, FWUPD_ERROR_NOT_SUPPORTED);
	g_clear_error(&error);
	g_assert_false(
	    klass->write_firmware(hub,
				  firmware,
				  progress,
				  FWUPD_INSTALL_FLAG_IGNORE_REQUIREMENTS | FWUPD_INSTALL_FLAG_FORCE,
				  &error));
	g_assert_error(error, FWUPD_ERROR, FWUPD_ERROR_NOT_SUPPORTED);
	g_clear_error(&error);
	g_assert_null(klass->dump_firmware(hub, progress, &error));
	g_assert_error(error, FWUPD_ERROR, FWUPD_ERROR_NOT_SUPPORTED);
	g_clear_error(&error);

	/* disconnected proxy is also refused */
	fu_device_set_proxy(hub, fake_proxy);
	fu_device_add_private_flag(fake_proxy, FU_DEVICE_PRIVATE_FLAG_UNCONNECTED);
	g_assert_false(klass->detach(hub, progress, &error));
	g_assert_error(error, FWUPD_ERROR, FWUPD_ERROR_NOT_FOUND);
	g_clear_error(&error);

	g_assert_cmpuint(spy.n_enter, ==, 0);
	g_assert_cmpuint(spy.n_reset, ==, 0);
	g_assert_cmpuint(spy.n_other, ==, 0);
}

static void
fu_genesys_usbhub_device_p34_parent_and_root_hub_func(void)
{
	g_autoptr(FuDevice) l1 = fu_genesys_test_parent_new(0x17EF, 0x1121);
	g_autoptr(FuDevice) l2 = fu_genesys_test_parent_new(0x17EF, 0x1122);
	g_autoptr(FuDevice) l3 = fu_genesys_test_parent_new(0x17EF, 0x1123);
	g_autoptr(FuDevice) foreign = fu_genesys_test_parent_new(0x05E3, 0x1121);
	g_autoptr(GError) error = NULL;

	fu_device_set_id(l1, "l1");
	fu_device_set_id(l2, "l2");
	fu_device_set_id(l3, "l3");

	g_assert_true(fu_genesys_usbhub_device_check_parent_hub(0x1121, 0x17EF, l1, &error));
	g_assert_no_error(error);
	g_assert_false(fu_genesys_usbhub_device_check_parent_hub(0x1121, 0x17EF, foreign, &error));
	g_assert_error(error, FWUPD_ERROR, FWUPD_ERROR_INVALID_DATA);
	g_clear_error(&error);
	g_assert_false(fu_genesys_usbhub_device_check_parent_hub(0x1121, 0x17EF, NULL, &error));
	g_assert_error(error, FWUPD_ERROR, FWUPD_ERROR_INVALID_DATA);
	g_clear_error(&error);

	g_assert_false(fu_genesys_usbhub_device_check_root_hub(0x1121, 0x17EF, l3, &error));
	g_assert_error(error, FWUPD_ERROR, FWUPD_ERROR_NOT_SUPPORTED);
	g_clear_error(&error);

	fu_device_add_child(l2, l3);
	g_assert_false(fu_genesys_usbhub_device_check_root_hub(0x1121, 0x17EF, l3, &error));
	g_assert_error(error, FWUPD_ERROR, FWUPD_ERROR_NOT_SUPPORTED);
	g_clear_error(&error);

	fu_device_add_child(l1, l2);
	g_assert_true(fu_genesys_usbhub_device_check_root_hub(0x1121, 0x17EF, l3, &error));
	g_assert_no_error(error);

	fu_device_add_private_flag(l1, FU_DEVICE_PRIVATE_FLAG_UNCONNECTED);
	g_assert_false(fu_genesys_usbhub_device_check_root_hub(0x1121, 0x17EF, l3, &error));
	g_assert_error(error, FWUPD_ERROR, FWUPD_ERROR_NOT_SUPPORTED);
	g_clear_error(&error);
}

static void
fu_genesys_usbhub_device_p34_setup_hub_chain_func(void)
{
	g_autoptr(FuContext) ctx = fu_genesys_test_context_new();
	struct {
		const gchar *name;
		gboolean has_l2;
		gboolean has_l1;
		guint16 l1_vid;
		guint16 l1_pid;
		FwupdError expected_error;
	} bad_cases[] = {
	    {"wrong-root-1151", TRUE, TRUE, 0x17EF, 0x1151, FWUPD_ERROR_NOT_SUPPORTED},
	    {"l2-only", TRUE, FALSE, 0, 0, FWUPD_ERROR_NOT_SUPPORTED},
	    {"no-parent", FALSE, FALSE, 0, 0, FWUPD_ERROR_INVALID_DATA},
	    {"wrong-ancestor-vid", TRUE, TRUE, 0x05E3, 0x1121, FWUPD_ERROR_NOT_SUPPORTED},
	};

	/* wrong upstream topologies fail in setup() before any vendor/ISP command */
	for (guint i = 0; i < G_N_ELEMENTS(bad_cases); i++) {
		g_autoptr(FuDevice) l1 =
		    bad_cases[i].has_l1
			? fu_genesys_test_parent_new(bad_cases[i].l1_vid, bad_cases[i].l1_pid)
			: NULL;
		g_autoptr(FuDevice) l2 = fu_genesys_test_parent_new(0x17EF, 0x1122);
		g_autoptr(FuDevice) l3 =
		    fu_genesys_test_hub_new(ctx, "USB\\VID_17EF&PID_1123", 0x1123);
		FuGenesysTestIspSpy spy = {0};
		g_autoptr(GError) error = NULL;

		if (l1 != NULL)
			fu_device_set_physical_id(l1, "1-4");
		fu_device_set_physical_id(l2, "1-4.1");
		fu_device_set_physical_id(l3, "1-4.1.2");
		if (bad_cases[i].has_l1)
			fu_genesys_test_set_backend_parent(ctx, l2, l1);
		if (bad_cases[i].has_l2)
			fu_genesys_test_set_backend_parent(ctx, l3, l2);
		else
			fu_genesys_test_set_backend_parent(ctx, l3, NULL);
		fu_genesys_usbhub_device_set_ctrl_hook(FU_GENESYS_USBHUB_DEVICE(l3),
						       fu_genesys_test_isp_spy_cb,
						       &spy);

		g_assert_false(FU_DEVICE_GET_CLASS(l3)->setup(l3, &error));
		g_assert_error(error, FWUPD_ERROR, bad_cases[i].expected_error);
		g_assert_cmpuint(spy.n_enter, ==, 0);
		g_assert_cmpuint(spy.n_reset, ==, 0);
		g_assert_cmpuint(spy.n_other, ==, 0);
	}

	/* extra intervening hop L3 -> L2 (1122) -> model B (1151) -> L1 (1121): must refuse */
	{
		g_autoptr(FuDevice) l1 = fu_genesys_test_parent_new(0x17EF, 0x1121);
		g_autoptr(FuDevice) mid_wrong = fu_genesys_test_parent_new(0x17EF, 0x1151);
		g_autoptr(FuDevice) l2 = fu_genesys_test_parent_new(0x17EF, 0x1122);
		g_autoptr(FuDevice) l3 =
		    fu_genesys_test_hub_new(ctx, "USB\\VID_17EF&PID_1123", 0x1123);
		FuGenesysTestIspSpy spy = {0};
		g_autoptr(GError) error = NULL;

		fu_device_set_physical_id(l1, "1-4");
		fu_device_set_physical_id(mid_wrong, "1-4.9");
		fu_device_set_physical_id(l2, "1-4.9.1");
		fu_device_set_physical_id(l3, "1-4.9.1.2");
		fu_genesys_test_set_backend_parent(ctx, mid_wrong, l1);
		fu_genesys_test_set_backend_parent(ctx, l2, mid_wrong);
		fu_genesys_test_set_backend_parent(ctx, l3, l2);
		fu_genesys_usbhub_device_set_ctrl_hook(FU_GENESYS_USBHUB_DEVICE(l3),
						       fu_genesys_test_isp_spy_cb,
						       &spy);

		g_assert_false(FU_DEVICE_GET_CLASS(l3)->setup(l3, &error));
		g_assert_error(error, FWUPD_ERROR, FWUPD_ERROR_NOT_SUPPORTED);
		g_assert_cmpuint(spy.n_enter, ==, 0);
		g_assert_cmpuint(spy.n_reset, ==, 0);
		g_assert_cmpuint(spy.n_other, ==, 0);
	}

	/* valid backend chain L3 -> L2 (1122) -> L1 (1121): clears topology gate in setup() */
	{
		g_autoptr(FuDevice) l1 = fu_genesys_test_parent_new(0x17EF, 0x1121);
		g_autoptr(FuDevice) l2 = fu_genesys_test_parent_new(0x17EF, 0x1122);
		g_autoptr(FuDevice) l3 =
		    fu_genesys_test_hub_new(ctx, "USB\\VID_17EF&PID_1123", 0x1123);
		g_autoptr(GError) error = NULL;

		fu_device_set_physical_id(l1, "1-4");
		fu_device_set_physical_id(l2, "1-4.1");
		fu_device_set_physical_id(l3, "1-4.1.2");
		fu_genesys_test_set_backend_parent(ctx, l2, l1);
		fu_genesys_test_set_backend_parent(ctx, l3, l2);

		/* passes ensure_hub_chain() and fails later on unopened USB descriptor read */
		g_assert_false(FU_DEVICE_GET_CLASS(l3)->setup(l3, &error));
		g_assert_nonnull(error);
		g_assert_null(g_strstr_len(error->message, -1, "topology"));
		g_assert_null(g_strstr_len(error->message, -1, "parent hub"));
	}
}

static void
fu_genesys_usbhub_device_p34_plugin_hub_lifecycle_func(void)
{
	g_autoptr(FuContext) ctx = fu_genesys_test_context_new();

	/* 1. Reverse arrival (L3 -> L2 -> L1): links full chain, propagates CompositeId, and
	 * registers exactly 3 distinct hubs with zero duplicate added/removed events */
	{
		g_autoptr(FuPlugin) plugin =
		    g_object_new(FU_TYPE_GENESYS_PLUGIN, "context", ctx, NULL);
		g_autoptr(FuDevice) l1 =
		    fu_genesys_test_hub_new(ctx, "USB\\VID_17EF&PID_1121", 0x1121);
		g_autoptr(FuDevice) l2 =
		    fu_genesys_test_hub_new(ctx, "USB\\VID_17EF&PID_1122", 0x1122);
		g_autoptr(FuDevice) l3 =
		    fu_genesys_test_hub_new(ctx, "USB\\VID_17EF&PID_1123", 0x1123);
		guint n_added = 0;
		guint n_removed = 0;
		GPtrArray *devices;

		g_signal_connect(plugin,
				 "device-added",
				 G_CALLBACK(fu_genesys_test_plugin_count_cb),
				 &n_added);
		g_signal_connect(plugin,
				 "device-removed",
				 G_CALLBACK(fu_genesys_test_plugin_count_cb),
				 &n_removed);

		fu_device_set_id(l1, "dock-a-l1");
		fu_device_set_id(l2, "dock-a-l2");
		fu_device_set_id(l3, "dock-a-l3");
		fu_device_set_physical_id(l1, "1-4");
		fu_device_set_physical_id(l2, "1-4.1");
		fu_device_set_physical_id(l3, "1-4.1.2");
		fu_genesys_test_set_backend_parent(ctx, l2, l1);
		fu_genesys_test_set_backend_parent(ctx, l3, l2);

		fu_genesys_test_plugin_add_device(plugin, l3);
		g_assert_null(fu_device_get_parent(l3, NULL));
		fu_genesys_test_plugin_add_device(plugin, l2);
		g_assert_true(fu_device_get_parent(l3, NULL) == l2);
		fu_genesys_test_plugin_add_device(plugin, l1);
		g_assert_true(fu_device_get_parent(l2, NULL) == l1);
		g_assert_true(fu_device_get_parent(l3, NULL) == l2);
		g_assert_cmpstr(fu_device_get_composite_id(l1), ==, fu_device_get_id(l1));
		g_assert_cmpstr(fu_device_get_composite_id(l2), ==, fu_device_get_id(l1));
		g_assert_cmpstr(fu_device_get_composite_id(l3), ==, fu_device_get_id(l1));

		devices = fu_plugin_get_devices(plugin);
		g_assert_cmpuint(devices->len, ==, 3);
		g_assert_cmpuint(n_added, ==, 3);
		g_assert_cmpuint(n_removed, ==, 0);
	}

	/* 1b. Normal arrival (L1 -> L2 -> L3) and L2 replug: no duplicate/removed events */
	{
		g_autoptr(FuPlugin) plugin =
		    g_object_new(FU_TYPE_GENESYS_PLUGIN, "context", ctx, NULL);
		g_autoptr(FuDevice) l1 =
		    fu_genesys_test_hub_new(ctx, "USB\\VID_17EF&PID_1121", 0x1121);
		g_autoptr(FuDevice) l2 =
		    fu_genesys_test_hub_new(ctx, "USB\\VID_17EF&PID_1122", 0x1122);
		g_autoptr(FuDevice) l2_replug =
		    fu_genesys_test_hub_new(ctx, "USB\\VID_17EF&PID_1122", 0x1122);
		g_autoptr(FuDevice) l3 =
		    fu_genesys_test_hub_new(ctx, "USB\\VID_17EF&PID_1123", 0x1123);
		guint n_added = 0;
		guint n_removed = 0;

		g_signal_connect(plugin,
				 "device-added",
				 G_CALLBACK(fu_genesys_test_plugin_count_cb),
				 &n_added);
		g_signal_connect(plugin,
				 "device-removed",
				 G_CALLBACK(fu_genesys_test_plugin_count_cb),
				 &n_removed);

		fu_device_set_id(l1, "dock-norm-l1");
		fu_device_set_id(l2, "dock-norm-l2");
		fu_device_set_id(l2_replug, "dock-norm-l2-replug");
		fu_device_set_id(l3, "dock-norm-l3");
		fu_device_set_physical_id(l1, "1-4");
		fu_device_set_physical_id(l2, "1-4.1");
		fu_device_set_physical_id(l2_replug, "1-4.1");
		fu_device_set_physical_id(l3, "1-4.1.2");
		fu_genesys_test_set_backend_parent(ctx, l2, l1);
		fu_genesys_test_set_backend_parent(ctx, l2_replug, l1);
		fu_genesys_test_set_backend_parent(ctx, l3, l2);

		fu_genesys_test_plugin_add_device(plugin, l1);
		fu_genesys_test_plugin_add_device(plugin, l2);
		fu_genesys_test_plugin_add_device(plugin, l3);
		g_assert_cmpuint(fu_plugin_get_devices(plugin)->len, ==, 3);
		g_assert_cmpuint(n_added, ==, 3);
		g_assert_cmpuint(n_removed, ==, 0);
		g_assert_cmpstr(fu_device_get_composite_id(l3), ==, fu_device_get_id(l1));

		/* L2 disconnects and replugs: L3 relinks to l2_replug without device-removed */
		fu_device_add_private_flag(l2, FU_DEVICE_PRIVATE_FLAG_UNCONNECTED);
		fu_genesys_test_set_backend_parent(ctx, l3, l2_replug);
		fu_genesys_test_plugin_add_device(plugin, l2_replug);
		g_assert_true(fu_device_get_parent(l2_replug, NULL) == l1);
		g_assert_true(fu_device_get_parent(l3, NULL) == l2_replug);
		g_assert_cmpstr(fu_device_get_composite_id(l3), ==, fu_device_get_id(l1));
		g_assert_cmpuint(n_added, ==, 4);
		g_assert_cmpuint(n_removed, ==, 0);
	}

	/* 2. Same PID on different physical parents: disambiguated strictly by physical_id */
	{
		g_autoptr(FuPlugin) plugin =
		    g_object_new(FU_TYPE_GENESYS_PLUGIN, "context", ctx, NULL);
		g_autoptr(FuDevice) l1_other = fu_genesys_test_parent_new(0x17EF, 0x1121);
		g_autoptr(FuDevice) l1_own = fu_genesys_test_parent_new(0x17EF, 0x1121);
		g_autoptr(FuDevice) l2_other =
		    fu_genesys_test_hub_new(ctx, "USB\\VID_17EF&PID_1122", 0x1122);
		g_autoptr(FuDevice) l2_own =
		    fu_genesys_test_hub_new(ctx, "USB\\VID_17EF&PID_1122", 0x1122);
		g_autoptr(FuDevice) l3 =
		    fu_genesys_test_hub_new(ctx, "USB\\VID_17EF&PID_1123", 0x1123);

		fu_device_set_id(l2_other, "dock-b-l2");
		fu_device_set_id(l2_own, "dock-a-l2");
		fu_device_set_id(l3, "dock-a-l3");
		fu_device_set_physical_id(l1_other, "1-5");
		fu_device_set_physical_id(l1_own, "1-4");
		fu_device_set_physical_id(l2_other, "1-5.1");
		fu_device_set_physical_id(l2_own, "1-4.1");
		fu_device_set_physical_id(l3, "1-4.1.2");
		fu_genesys_test_set_backend_parent(ctx, l2_other, l1_other);
		fu_genesys_test_set_backend_parent(ctx, l2_own, l1_own);
		fu_genesys_test_set_backend_parent(ctx, l3, l2_own);

		fu_genesys_test_plugin_add_device(plugin, l2_other);
		fu_genesys_test_plugin_add_device(plugin, l3);
		g_assert_null(fu_device_get_parent(l3, NULL));
		fu_genesys_test_plugin_add_device(plugin, l2_own);
		g_assert_true(fu_device_get_parent(l3, NULL) == l2_own);
	}

	/* 3. Stale connected parent (same VID/PID, changed physical ID) and ancestor backend
	 * drift (L2's USB parent changes to model B 1151): both fail closed with zero ISP_ENTER */
	{
		g_autoptr(FuPlugin) plugin =
		    g_object_new(FU_TYPE_GENESYS_PLUGIN, "context", ctx, NULL);
		g_autoptr(FuDevice) l1_a =
		    fu_genesys_test_hub_new(ctx, "USB\\VID_17EF&PID_1121", 0x1121);
		g_autoptr(FuDevice) l1_b = fu_genesys_test_parent_new(0x17EF, 0x1151);
		g_autoptr(FuDevice) l2_old =
		    fu_genesys_test_hub_new(ctx, "USB\\VID_17EF&PID_1122", 0x1122);
		g_autoptr(FuDevice) l2_new =
		    fu_genesys_test_hub_new(ctx, "USB\\VID_17EF&PID_1122", 0x1122);
		g_autoptr(FuDevice) l3 =
		    fu_genesys_test_hub_new(ctx, "USB\\VID_17EF&PID_1123", 0x1123);
		g_autoptr(FuDevice) proxy_l3 = g_object_new(FU_TYPE_GENESYS_HUBHID_DEVICE, NULL);
		FuDeviceClass *klass = FU_DEVICE_GET_CLASS(l3);
		FuGenesysTestIspSpy spy = {0};
		guint n_added = 0;
		guint n_removed = 0;
		g_autoptr(FuProgress) progress = fu_progress_new(G_STRLOC);
		g_autoptr(FuFirmware) firmware = fu_firmware_new();
		g_autoptr(GError) error = NULL;

		g_signal_connect(plugin,
				 "device-added",
				 G_CALLBACK(fu_genesys_test_plugin_count_cb),
				 &n_added);
		g_signal_connect(plugin,
				 "device-removed",
				 G_CALLBACK(fu_genesys_test_plugin_count_cb),
				 &n_removed);

		fu_device_set_id(l1_a, "drift-l1-a");
		fu_device_set_id(l2_old, "drift-l2-old");
		fu_device_set_id(l2_new, "drift-l2-new");
		fu_device_set_id(l3, "drift-l3");
		fu_device_set_physical_id(l1_a, "1-4");
		fu_device_set_physical_id(l1_b, "1-4");
		fu_device_set_physical_id(l2_old, "1-4.1");
		fu_device_set_physical_id(l2_new, "1-5.1");
		fu_device_set_physical_id(l3, "1-4.1.2");
		fu_genesys_test_set_backend_parent(ctx, l2_old, l1_a);
		fu_genesys_test_set_backend_parent(ctx, l2_new, l1_a);
		fu_genesys_test_set_backend_parent(ctx, l3, l2_old);
		fu_device_set_vid(proxy_l3, 0x17EF);
		fu_device_set_pid(proxy_l3, 0x4C42);
		fu_genesys_test_set_backend_parent(ctx, proxy_l3, l3);
		fu_device_set_proxy(l3, proxy_l3);
		fu_genesys_usbhub_device_set_ctrl_hook(FU_GENESYS_USBHUB_DEVICE(l3),
						       fu_genesys_test_isp_spy_cb,
						       &spy);

		fu_genesys_test_plugin_add_device(plugin, l1_a);
		fu_genesys_test_plugin_add_device(plugin, l2_old);
		fu_genesys_test_plugin_add_device(plugin, l3);
		g_assert_true(fu_device_get_parent(l3, NULL) == l2_old);

		/* stale connected parent with changed physical ID */
		fu_genesys_test_set_backend_parent(ctx, l3, l2_new);
		g_assert_false(klass->detach(l3, progress, &error));
		g_assert_error(error, FWUPD_ERROR, FWUPD_ERROR_INVALID_DATA);
		g_assert_nonnull(g_strstr_len(error->message, -1, "does not match"));
		g_clear_error(&error);
		g_assert_false(klass->write_firmware(l3,
						     firmware,
						     progress,
						     FWUPD_INSTALL_FLAG_IGNORE_REQUIREMENTS |
							 FWUPD_INSTALL_FLAG_FORCE,
						     &error));
		g_assert_error(error, FWUPD_ERROR, FWUPD_ERROR_INVALID_DATA);
		g_clear_error(&error);

		/* relink to l2_new without triggering device-removed, then drift l2_new's backend
		 * parent to model B (1151) */
		fu_genesys_test_plugin_add_device(plugin, l2_new);
		g_assert_true(fu_device_get_parent(l3, NULL) == l2_new);
		g_assert_cmpstr(fu_device_get_composite_id(l3), ==, fu_device_get_id(l1_a));
		g_assert_cmpuint(n_added, ==, 4);
		g_assert_cmpuint(n_removed, ==, 0);
		fu_genesys_test_set_backend_parent(ctx, l2_new, l1_b);
		g_assert_false(klass->detach(l3, progress, &error));
		g_assert_error(error, FWUPD_ERROR, FWUPD_ERROR_INVALID_DATA);
		g_assert_nonnull(g_strstr_len(error->message, -1, "17ef:1151"));
		g_clear_error(&error);

		g_assert_cmpuint(spy.n_enter, ==, 0);
		g_assert_cmpuint(spy.n_reset, ==, 0);
		g_assert_cmpuint(spy.n_other, ==, 0);
	}
}

static void
fu_genesys_usbhub_device_p34_strict_project_check_func(void)
{
	const guint8 proj_a[15] = "P34WD40-HUB-L3 ";
	const guint8 proj_b[15] = "P34WD40-HUB-L2 ";
	const guint8 proj_blank[15] = {0};
	g_autoptr(GBytes) bytes_a = g_bytes_new_static(proj_a, sizeof(proj_a));
	g_autoptr(GBytes) bytes_b = g_bytes_new_static(proj_b, sizeof(proj_b));
	g_autoptr(GBytes) bytes_blank = g_bytes_new_static(proj_blank, sizeof(proj_blank));
	g_autoptr(GError) error = NULL;

	/* exact match passes */
	g_assert_true(fu_genesys_usbhub_device_check_project_compatibility(TRUE,
									   bytes_a,
									   "352350",
									   bytes_a,
									   "352350",
									   &error));
	g_assert_no_error(error);

	/* mismatched PROJECT refuses */
	g_assert_false(fu_genesys_usbhub_device_check_project_compatibility(TRUE,
									    bytes_a,
									    "352350",
									    bytes_b,
									    "352350",
									    &error));
	g_assert_error(error, FWUPD_ERROR, FWUPD_ERROR_INVALID_FILE);
	g_clear_error(&error);

	/* mismatched IC refuses */
	g_assert_false(fu_genesys_usbhub_device_check_project_compatibility(TRUE,
									    bytes_a,
									    "352350",
									    bytes_a,
									    "352510",
									    &error));
	g_assert_error(error, FWUPD_ERROR, FWUPD_ERROR_INVALID_FILE);
	g_clear_error(&error);

	/* blank device identity refuses even if firmware is also blank */
	g_assert_false(fu_genesys_usbhub_device_check_project_compatibility(TRUE,
									    bytes_blank,
									    "352350",
									    bytes_blank,
									    "352350",
									    &error));
	g_assert_error(error, FWUPD_ERROR, FWUPD_ERROR_NOT_SUPPORTED);
	g_clear_error(&error);

	/* missing firmware PROJECT refuses */
	g_assert_false(fu_genesys_usbhub_device_check_project_compatibility(TRUE,
									    bytes_a,
									    "352350",
									    NULL,
									    "352350",
									    &error));
	g_assert_error(error, FWUPD_ERROR, FWUPD_ERROR_INVALID_FILE);
	g_clear_error(&error);

	/* 0xFF-filled (erased flash), space-filled PROJECT, and blank IC also refuse */
	{
		const guint8 proj_ff[15] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
					    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
		const guint8 proj_sp[15] = "               ";
		g_autoptr(GBytes) bytes_ff = g_bytes_new_static(proj_ff, sizeof(proj_ff));
		g_autoptr(GBytes) bytes_sp = g_bytes_new_static(proj_sp, sizeof(proj_sp));

		g_assert_false(fu_genesys_usbhub_device_check_project_compatibility(TRUE,
										    bytes_ff,
										    "352350",
										    bytes_ff,
										    "352350",
										    &error));
		g_assert_error(error, FWUPD_ERROR, FWUPD_ERROR_NOT_SUPPORTED);
		g_clear_error(&error);

		g_assert_false(fu_genesys_usbhub_device_check_project_compatibility(TRUE,
										    bytes_sp,
										    "352350",
										    bytes_sp,
										    "352350",
										    &error));
		g_assert_error(error, FWUPD_ERROR, FWUPD_ERROR_NOT_SUPPORTED);
		g_clear_error(&error);

		g_assert_false(fu_genesys_usbhub_device_check_project_compatibility(TRUE,
										    bytes_a,
										    "",
										    bytes_a,
										    "352350",
										    &error));
		g_assert_error(error, FWUPD_ERROR, FWUPD_ERROR_NOT_SUPPORTED);
		g_clear_error(&error);
	}

	/* non-strict legacy hubs bypass strict PROJECT/IC check */
	g_assert_true(fu_genesys_usbhub_device_check_project_compatibility(FALSE,
									   bytes_blank,
									   "352350",
									   bytes_b,
									   "352510",
									   &error));
	g_assert_no_error(error);
}

static void
fu_genesys_usbhub_device_p34_dual_bank_safety_func(void)
{
	gboolean backup = FALSE;
	g_autoptr(GError) error = NULL;
	g_autoptr(GError) sig_err =
	    g_error_new_literal(FWUPD_ERROR, FWUPD_ERROR_SIGNATURE_INVALID, "bad sig");
	g_autoptr(GError) read_err =
	    g_error_new_literal(FWUPD_ERROR, FWUPD_ERROR_READ, "io failed");

	g_assert_cmpint(fu_genesys_usbhub_device_bank_state_from_error(sig_err),
			==,
			FU_GENESYS_USBHUB_BANK_STATE_INVALID);
	g_assert_cmpint(fu_genesys_usbhub_device_bank_state_from_error(read_err),
			==,
			FU_GENESYS_USBHUB_BANK_STATE_UNKNOWN);
	{
		const guint8 bad_bank[] = {0xFF, 0xFF, 0xFF, 0xFF};
		const guint8 good_bank[] = {0x01, 0x02, 0x00, 0x03};
		g_assert_cmpint(
		    fu_genesys_usbhub_device_bank_state_from_image(bad_bank, sizeof(bad_bank)),
		    ==,
		    FU_GENESYS_USBHUB_BANK_STATE_INVALID);
		g_assert_cmpint(
		    fu_genesys_usbhub_device_bank_state_from_image(good_bank, sizeof(good_bank)),
		    ==,
		    FU_GENESYS_USBHUB_BANK_STATE_VALID);
		g_assert_cmpint(fu_genesys_usbhub_device_bank_state_from_image(good_bank, 1),
				==,
				FU_GENESYS_USBHUB_BANK_STATE_UNKNOWN);
	}

	/* GL3523 backs up bank1 to bank2 and writes bank1; GL3525 writes bank2 */
	g_assert_false(fu_genesys_usbhub_device_select_update_bank2(TRUE, 0x8716, 0x8715, &backup));
	g_assert_true(backup);
	g_assert_true(fu_genesys_usbhub_device_select_update_bank2(FALSE, 0x8625, 0x8624, &backup));
	g_assert_false(backup);
	/* bank1 <= bank2 writes bank1 without backup on both GL3523 and GL3525 */
	g_assert_false(fu_genesys_usbhub_device_select_update_bank2(TRUE, 0x8715, 0x8716, &backup));
	g_assert_false(backup);
	g_assert_false(fu_genesys_usbhub_device_select_update_bank2(FALSE, 0x8624, 0x8624, &backup));
	g_assert_false(backup);

	/* invalid bank code sizes (kbs == 0, < 2 bytes, or exceeding capacity) must fail closed */
	g_assert_false(fu_genesys_usbhub_device_validate_bank_code_size(0, 0xB000, &error));
	g_assert_error(error, FWUPD_ERROR, FWUPD_ERROR_INVALID_DATA);
	g_clear_error(&error);
	g_assert_false(fu_genesys_usbhub_device_validate_bank_code_size(1, 0xB000, &error));
	g_assert_error(error, FWUPD_ERROR, FWUPD_ERROR_INVALID_DATA);
	g_clear_error(&error);
	g_assert_false(fu_genesys_usbhub_device_validate_bank_code_size(0xB400, 0xB000, &error));
	g_assert_error(error, FWUPD_ERROR, FWUPD_ERROR_INVALID_DATA);
	g_clear_error(&error);
	g_assert_true(fu_genesys_usbhub_device_validate_bank_code_size(0xB000, 0xB000, &error));
	g_assert_no_error(error);

	/* unknown bank state always refuses */
	g_assert_false(
	    fu_genesys_usbhub_device_check_update_bank(TRUE,
						       TRUE,
						       TRUE,
						       FU_GENESYS_USBHUB_BANK_STATE_UNKNOWN,
						       FU_GENESYS_USBHUB_BANK_STATE_VALID,
						       0x8625,
						       0x8624,
						       TRUE,
						       0x8626,
						       &error));
	g_assert_error(error, FWUPD_ERROR, FWUPD_ERROR_NOT_SUPPORTED);
	g_clear_error(&error);

	/* GL3525 refuses target older than the other bank */
	g_assert_false(
	    fu_genesys_usbhub_device_check_update_bank(TRUE,
						       TRUE,
						       TRUE,
						       FU_GENESYS_USBHUB_BANK_STATE_VALID,
						       FU_GENESYS_USBHUB_BANK_STATE_VALID,
						       0x8625,
						       0x8624,
						       TRUE,
						       0x8624,
						       &error));
	g_assert_error(error, FWUPD_ERROR, FWUPD_ERROR_NOT_SUPPORTED);
	g_clear_error(&error);

	/* GL3525 accepts target >= other bank (both greater and equal, writing bank2 or bank1) */
	g_assert_true(fu_genesys_usbhub_device_check_update_bank(TRUE,
								 TRUE,
								 TRUE,
								 FU_GENESYS_USBHUB_BANK_STATE_VALID,
								 FU_GENESYS_USBHUB_BANK_STATE_VALID,
								 0x8625,
								 0x8624,
								 TRUE,
								 0x8626,
								 &error));
	g_assert_no_error(error);
	g_assert_true(fu_genesys_usbhub_device_check_update_bank(TRUE,
								 TRUE,
								 FALSE,
								 FU_GENESYS_USBHUB_BANK_STATE_VALID,
								 FU_GENESYS_USBHUB_BANK_STATE_VALID,
								 0x8624,
								 0x8625,
								 TRUE,
								 0x8625,
								 &error));
	g_assert_no_error(error);
	g_assert_false(
	    fu_genesys_usbhub_device_check_update_bank(TRUE,
						       TRUE,
						       FALSE,
						       FU_GENESYS_USBHUB_BANK_STATE_VALID,
						       FU_GENESYS_USBHUB_BANK_STATE_VALID,
						       0x8624,
						       0x8625,
						       TRUE,
						       0x8624,
						       &error));
	g_assert_error(error, FWUPD_ERROR, FWUPD_ERROR_NOT_SUPPORTED);
	g_clear_error(&error);

	/* GL3525 refuses when the running other bank failed checksum (INVALID + running) */
	g_assert_false(
	    fu_genesys_usbhub_device_check_update_bank(TRUE,
						       TRUE,
						       TRUE,
						       FU_GENESYS_USBHUB_BANK_STATE_INVALID,
						       FU_GENESYS_USBHUB_BANK_STATE_VALID,
						       0x0000,
						       0x8624,
						       TRUE,
						       0x8625,
						       &error));
	g_assert_error(error, FWUPD_ERROR, FWUPD_ERROR_NOT_SUPPORTED);
	g_clear_error(&error);

	/* GL3525 refuses erasing the only checksum-valid bank (even when both versions are 0x0000
	 * or when the valid bank is not the running other bank) */
	g_assert_false(
	    fu_genesys_usbhub_device_check_update_bank(TRUE,
						       TRUE,
						       FALSE,
						       FU_GENESYS_USBHUB_BANK_STATE_VALID,
						       FU_GENESYS_USBHUB_BANK_STATE_INVALID,
						       0x0000,
						       0x0000,
						       FALSE,
						       0x8625,
						       &error));
	g_assert_error(error, FWUPD_ERROR, FWUPD_ERROR_NOT_SUPPORTED);
	g_clear_error(&error);

	g_assert_false(
	    fu_genesys_usbhub_device_check_update_bank(TRUE,
						       TRUE,
						       TRUE,
						       FU_GENESYS_USBHUB_BANK_STATE_INVALID,
						       FU_GENESYS_USBHUB_BANK_STATE_VALID,
						       0x0000,
						       0x8624,
						       FALSE,
						       0x8625,
						       &error));
	g_assert_error(error, FWUPD_ERROR, FWUPD_ERROR_NOT_SUPPORTED);
	g_clear_error(&error);

	/* GL3525 allows writing an INVALID target bank when the other bank is VALID and older */
	g_assert_true(
	    fu_genesys_usbhub_device_check_update_bank(TRUE,
						       TRUE,
						       FALSE,
						       FU_GENESYS_USBHUB_BANK_STATE_INVALID,
						       FU_GENESYS_USBHUB_BANK_STATE_VALID,
						       0x0000,
						       0x8624,
						       TRUE,
						       0x8625,
						       &error));
	g_assert_no_error(error);

	/* GL3525 allows when both banks are INVALID and the other bank is not running */
	g_assert_true(
	    fu_genesys_usbhub_device_check_update_bank(TRUE,
						       TRUE,
						       FALSE,
						       FU_GENESYS_USBHUB_BANK_STATE_INVALID,
						       FU_GENESYS_USBHUB_BANK_STATE_INVALID,
						       0x0000,
						       0x0000,
						       FALSE,
						       0x8625,
						       &error));
	g_assert_no_error(error);

	/* GL3523 also refuses UNKNOWN bank state */
	g_assert_false(
	    fu_genesys_usbhub_device_check_update_bank(TRUE,
						       FALSE,
						       FALSE,
						       FU_GENESYS_USBHUB_BANK_STATE_VALID,
						       FU_GENESYS_USBHUB_BANK_STATE_UNKNOWN,
						       0x8716,
						       0x0000,
						       FALSE,
						       0x8717,
						       &error));
	g_assert_error(error, FWUPD_ERROR, FWUPD_ERROR_NOT_SUPPORTED);
	g_clear_error(&error);
}

/* runtime HID proxy verification (VID/PID, live backend parent physical ID, disconnect,
 * and replug rebinding) */
static void
fu_genesys_usbhub_device_p34_hid_proxy_runtime_verify_func(void)
{
	g_autoptr(FuContext) ctx = fu_genesys_test_context_new();
	g_autoptr(FuDevice) hub = fu_genesys_test_hub_new(ctx, "USB\\VID_17EF&PID_1121", 0x1121);
	g_autoptr(FuDevice) other_hub = fu_genesys_test_parent_new(0x17EF, 0x1121);
	g_autoptr(FuDevice) proxy_ok = g_object_new(FU_TYPE_GENESYS_HUBHID_DEVICE, NULL);
	g_autoptr(FuDevice) proxy_wrong_pid = g_object_new(FU_TYPE_GENESYS_HUBHID_DEVICE, NULL);
	g_autoptr(FuDevice) proxy_replugged = g_object_new(FU_TYPE_GENESYS_HUBHID_DEVICE, NULL);
	FuDeviceClass *klass = FU_DEVICE_GET_CLASS(hub);
	FuGenesysTestIspSpy spy = {0};
	g_autoptr(FuProgress) progress = fu_progress_new(G_STRLOC);
	g_autoptr(GError) error = NULL;

	fu_device_set_physical_id(hub, "1-4");
	fu_device_set_physical_id(other_hub, "1-5");
	fu_device_set_vid(proxy_ok, 0x17EF);
	fu_device_set_pid(proxy_ok, 0xF00F);
	fu_device_set_vid(proxy_wrong_pid, 0x17EF);
	fu_device_set_pid(proxy_wrong_pid, 0x4C42);
	fu_device_set_vid(proxy_replugged, 0x17EF);
	fu_device_set_pid(proxy_replugged, 0xF00F);
	fu_genesys_usbhub_device_set_ctrl_hook(FU_GENESYS_USBHUB_DEVICE(hub),
					       fu_genesys_test_isp_spy_cb,
					       &spy);

	/* matching VID/PID and live backend parent physical ID passes */
	fu_genesys_test_set_backend_parent(ctx, proxy_ok, hub);
	fu_device_set_proxy(hub, proxy_ok);
	g_assert_true(fu_genesys_usbhub_device_verify_hid_proxy(FU_GENESYS_USBHUB_DEVICE(hub),
								proxy_ok,
								&error));
	g_assert_no_error(error);

	/* wrong HID proxy PID (4C42 on L1 expecting F00F) refused in detach() AND attach() */
	fu_genesys_test_set_backend_parent(ctx, proxy_wrong_pid, hub);
	fu_device_set_proxy(hub, proxy_wrong_pid);
	g_assert_false(klass->detach(hub, progress, &error));
	g_assert_error(error, FWUPD_ERROR, FWUPD_ERROR_INVALID_DATA);
	g_assert_nonnull(g_strstr_len(error->message, -1, "does not match expected"));
	g_clear_error(&error);
	g_assert_false(klass->attach(hub, progress, &error));
	g_assert_error(error, FWUPD_ERROR, FWUPD_ERROR_INVALID_DATA);
	g_assert_nonnull(g_strstr_len(error->message, -1, "does not match expected"));
	g_clear_error(&error);

	/* proxy whose backend parent moved to another dock (same VID/PID, different physical ID) */
	fu_genesys_test_set_backend_parent(ctx, proxy_ok, other_hub);
	fu_device_set_proxy(hub, proxy_ok);
	g_assert_false(klass->detach(hub, progress, &error));
	g_assert_error(error, FWUPD_ERROR, FWUPD_ERROR_INVALID_DATA);
	g_assert_nonnull(g_strstr_len(error->message, -1, "HID proxy parent"));
	g_clear_error(&error);
	g_assert_false(klass->attach(hub, progress, &error));
	g_assert_error(error, FWUPD_ERROR, FWUPD_ERROR_INVALID_DATA);
	g_assert_nonnull(g_strstr_len(error->message, -1, "HID proxy parent"));
	g_clear_error(&error);

	/* unresolvable backend parent fails closed in both detach() and attach() */
	fu_genesys_test_set_backend_parent(ctx, proxy_ok, NULL);
	g_assert_false(klass->detach(hub, progress, &error));
	g_assert_error(error, FWUPD_ERROR, FWUPD_ERROR_INVALID_DATA);
	g_assert_nonnull(g_strstr_len(error->message, -1, "topology cannot be resolved"));
	g_clear_error(&error);
	g_assert_false(klass->attach(hub, progress, &error));
	g_assert_error(error, FWUPD_ERROR, FWUPD_ERROR_INVALID_DATA);
	g_assert_nonnull(g_strstr_len(error->message, -1, "topology cannot be resolved"));
	g_clear_error(&error);

	/* disconnected proxy fails closed in attach() (zero transfers) */
	fu_genesys_test_set_backend_parent(ctx, proxy_ok, hub);
	fu_device_add_private_flag(proxy_ok, FU_DEVICE_PRIVATE_FLAG_UNCONNECTED);
	g_assert_false(klass->attach(hub, progress, &error));
	g_assert_error(error, FWUPD_ERROR, FWUPD_ERROR_NOT_FOUND);
	g_assert_nonnull(g_strstr_len(error->message, -1, "no longer connected"));
	g_clear_error(&error);

	/* zero vendor commands sent across all negative cases */
	g_assert_cmpuint(spy.n_enter, ==, 0);
	g_assert_cmpuint(spy.n_reset, ==, 0);
	g_assert_cmpuint(spy.n_other, ==, 0);

	/* replugged proxy with matching VID/PID and physical parent succeeds in attach() */
	spy.allow_reset = TRUE;
	fu_genesys_test_set_backend_parent(ctx, proxy_replugged, hub);
	fu_device_set_proxy(hub, proxy_replugged);
	g_assert_true(fu_genesys_usbhub_device_verify_hid_proxy(FU_GENESYS_USBHUB_DEVICE(hub),
								proxy_replugged,
								&error));
	g_assert_no_error(error);
	g_assert_true(klass->attach(hub, progress, &error));
	g_assert_no_error(error);
	g_assert_cmpuint(spy.n_reset, ==, 1);
	g_assert_true(fu_device_has_flag(hub, FWUPD_DEVICE_FLAG_WAIT_FOR_REPLUG));
}

static GBytes *
fu_genesys_test_build_fw_image(gsize static_off,
			       gsize project_off,
			       const gchar *ic_type,
			       const gchar *project_str)
{
	const gsize sz = 0x1000;
	g_autofree guint8 *buf = g_malloc0(sz);
	guint16 checksum = 0;

	/* code size at 0xFB (0x1000 / 1024 = 4) and XROM magic at 0xFC..0xFF */
	buf[0xFB] = 0x04;
	g_assert_true(fu_memcpy_safe(buf, sz, 0xFC, (const guint8 *)"XROM", 4, 0, 4, NULL));
	/* static tool string: version=BrandProject ('8'=0x38) at +0x00, mask IC type at +0x08 */
	buf[static_off + 0x00] = '8';
	g_assert_true(
	    fu_memcpy_safe(buf, sz, static_off + 0x08, (const guint8 *)ic_type, 6, 0, 6, NULL));
	/* 15-byte brand project tool string */
	g_assert_true(
	    fu_memcpy_safe(buf, sz, project_off, (const guint8 *)project_str, 15, 0, 15, NULL));
	/* version pair at 0x10E */
	buf[0x10E] = 0x25;
	buf[0x10F] = 0x86;
	/* 16-bit big-endian sum checksum in last 2 bytes */
	for (gsize i = 0; i < sz - 2; i++)
		checksum += buf[i];
	buf[sz - 2] = (checksum >> 8) & 0xFF;
	buf[sz - 1] = checksum & 0xFF;
	return g_bytes_new_take(g_steal_pointer(&buf), sz);
}

/* real FuGenesysUsbhubFirmware parsing of GL3525 (L1) and GL352350 (L2/L3) images
 * into PROJECT/IC extraction and 3x3 strict compatibility matrix, plus P34 CFI C84012 quirk */
static void
fu_genesys_usbhub_device_p34_firmware_project_parse_func(void)
{
	struct {
		gsize static_off;
		gsize project_off;
		const gchar *ic_type;
		const gchar *project;
	} specs[3] = {
	    {0x251, 0x2B1, "352510", "LNV_P34WD40_L10"},
	    {0x221, 0x281, "352350", "LNV_P34WD40_L20"},
	    {0x221, 0x281, "352350", "LNV_P34WD40_L30"},
	};
	g_autoptr(GPtrArray) parsed =
	    g_ptr_array_new_with_free_func((GDestroyNotify)g_object_unref);

	for (guint i = 0; i < G_N_ELEMENTS(specs); i++) {
		g_autoptr(FuFirmware) fw = g_object_new(FU_TYPE_GENESYS_USBHUB_FIRMWARE, NULL);
		g_autoptr(GBytes) blob = fu_genesys_test_build_fw_image(specs[i].static_off,
									specs[i].project_off,
									specs[i].ic_type,
									specs[i].project);
		g_autoptr(GBytes) proj = NULL;
		g_autofree gchar *ic = NULL;
		g_autoptr(GError) error = NULL;
		gsize projsz = 0;

		g_assert_true(fu_firmware_parse_bytes(fw,
						      blob,
						      0x0,
						      FU_FIRMWARE_PARSE_FLAG_NO_SEARCH,
						      &error));
		g_assert_no_error(error);

		proj = fu_genesys_usbhub_firmware_get_project_bytes(FU_GENESYS_USBHUB_FIRMWARE(fw));
		ic = fu_genesys_usbhub_firmware_get_mask_project_ic_type(
		    FU_GENESYS_USBHUB_FIRMWARE(fw));
		g_assert_nonnull(proj);
		g_assert_cmpuint(g_bytes_get_size(proj), ==, 15);
		g_assert_cmpmem(g_bytes_get_data(proj, &projsz), projsz, specs[i].project, 15);
		g_assert_cmpstr(ic, ==, specs[i].ic_type);
		g_ptr_array_add(parsed, g_steal_pointer(&fw));
	}

	/* 3x3 strict compatibility matrix using real parsed firmware objects */
	for (guint dev_i = 0; dev_i < 3; dev_i++) {
		g_autoptr(GBytes) dev_proj = g_bytes_new_static(specs[dev_i].project, 15);
		for (guint fw_i = 0; fw_i < 3; fw_i++) {
			FuGenesysUsbhubFirmware *fw = g_ptr_array_index(parsed, fw_i);
			g_autoptr(GBytes) fw_proj =
			    fu_genesys_usbhub_firmware_get_project_bytes(fw);
			g_autofree gchar *fw_ic =
			    fu_genesys_usbhub_firmware_get_mask_project_ic_type(fw);
			g_autoptr(GError) error = NULL;
			gboolean ret = fu_genesys_usbhub_device_check_project_compatibility(
			    TRUE,
			    dev_proj,
			    specs[dev_i].ic_type,
			    fw_proj,
			    fw_ic,
			    &error);
			if (dev_i == fw_i) {
				g_assert_true(ret);
				g_assert_no_error(error);
			} else {
				g_assert_false(ret);
				g_assert_error(error, FWUPD_ERROR, FWUPD_ERROR_INVALID_FILE);
			}
		}
	}

	/* verify P34 flash chip C84012 (GD25Q20C) resolves from cfi.quirk; never silently skip */
	{
		g_autofree gchar *installed_cfi =
		    g_test_build_filename(G_TEST_DIST, "tests", "cfi", NULL);
		g_autofree gchar *source_cfi =
		    g_test_build_filename(G_TEST_DIST, "..", "..", "data", NULL);
		const gchar *cfi_dir =
		    g_file_test(installed_cfi, G_FILE_TEST_IS_DIR) ? installed_cfi : source_cfi;
		g_autoptr(FuContext) ctx = fu_context_new_full(FU_CONTEXT_FLAG_NO_CACHE);
		g_autoptr(FuProgress) progress = fu_progress_new(G_STRLOC);
		g_autoptr(FuDevice) parent_dev = fu_device_new(ctx);
		g_autoptr(FuCfiDevice) cfi = NULL;
		g_autoptr(GError) error = NULL;

		g_assert_true(g_file_test(cfi_dir, G_FILE_TEST_IS_DIR));
		fu_context_set_path(ctx, FU_PATH_KIND_DATADIR_QUIRKS, cfi_dir);
		g_assert_true(fu_context_load(ctx, progress, FU_CONTEXT_LOAD_FLAG_NONE, &error));
		g_assert_no_error(error);
		cfi = fu_cfi_device_new(parent_dev, "C84012");
		g_assert_true(fu_device_probe(FU_DEVICE(cfi), &error));
		g_assert_no_error(error);
		g_assert_cmpuint(fu_cfi_device_get_page_size(cfi), ==, 0x100);
		g_assert_cmpuint(fu_cfi_device_get_sector_size(cfi), ==, 0x1000);
		g_assert_cmpuint(fu_cfi_device_get_block_size(cfi), ==, 0x10000);
	}
}

/* GL3525 Bank 2 starts at 0xB000 with size 0xB000: a 0xFFC0 packet must split at 0x10000 */
static void
fu_genesys_usbhub_device_p34_chunks_page_boundary_func(void)
{
	g_autofree guint8 *buf = g_malloc0(0xB000);
	g_autoptr(GPtrArray) chunks = NULL;
	g_autoptr(GError) error = NULL;
	FuChunk *chk0;
	FuChunk *chk1;

	chunks = fu_chunk_array_new(buf, 0xB000, 0xB000, 0x10000, 0xFFC0, &error);
	g_assert_no_error(error);
	g_assert_nonnull(chunks);
	g_assert_cmpuint(chunks->len, ==, 2);

	chk0 = g_ptr_array_index(chunks, 0);
	g_assert_cmpuint(fu_chunk_get_page(chk0), ==, 0);
	g_assert_cmpuint(fu_chunk_get_address(chk0), ==, 0xB000);
	g_assert_cmpuint(fu_chunk_get_data_sz(chk0), ==, 0x5000);

	chk1 = g_ptr_array_index(chunks, 1);
	g_assert_cmpuint(fu_chunk_get_page(chk1), ==, 1);
	g_assert_cmpuint(fu_chunk_get_address(chk1), ==, 0x0000);
	g_assert_cmpuint(fu_chunk_get_data_sz(chk1), ==, 0x6000);
}

int
main(int argc, char **argv)
{
	(void)g_setenv("G_TEST_SRCDIR", SRCDIR, FALSE);
	g_test_init(&argc, &argv, NULL);
	g_test_add_func("/fwupd/genesys/p34/quirk", fu_genesys_usbhub_device_p34_quirk_func);
	g_test_add_func("/fwupd/genesys/p34/proxy-required-fail-closed",
			fu_genesys_usbhub_device_p34_proxy_required_fail_closed_func);
	g_test_add_func("/fwupd/genesys/p34/parent-and-root-hub",
			fu_genesys_usbhub_device_p34_parent_and_root_hub_func);
	g_test_add_func("/fwupd/genesys/p34/setup-hub-chain",
			fu_genesys_usbhub_device_p34_setup_hub_chain_func);
	g_test_add_func("/fwupd/genesys/p34/plugin-hub-lifecycle",
			fu_genesys_usbhub_device_p34_plugin_hub_lifecycle_func);
	g_test_add_func("/fwupd/genesys/p34/strict-project-check",
			fu_genesys_usbhub_device_p34_strict_project_check_func);
	g_test_add_func("/fwupd/genesys/p34/dual-bank-safety",
			fu_genesys_usbhub_device_p34_dual_bank_safety_func);
	g_test_add_func("/fwupd/genesys/p34/hid-proxy-runtime-verify",
			fu_genesys_usbhub_device_p34_hid_proxy_runtime_verify_func);
	g_test_add_func("/fwupd/genesys/p34/firmware-project-parse",
			fu_genesys_usbhub_device_p34_firmware_project_parse_func);
	g_test_add_func("/fwupd/genesys/p34/chunks-page-boundary",
			fu_genesys_usbhub_device_p34_chunks_page_boundary_func);
	return g_test_run();
}
