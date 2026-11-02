/*
 * Copyright 2026 Genesys Logic, Inc.
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include <glib/gstdio.h>

/*
 * The test vectors are literal hardware identifiers (PIDs, flash opcodes and offsets), so the
 * count of inline magic values is expected to be high.
 * nocheck:magic-inlines=229
 */

#include "fu-cfi-device.h"
#include "fu-context-private.h"
#include "fu-device-private.h"
#include "fu-genesys-hubhid-device.h"
#include "fu-genesys-usbhub-device.h"
#include "fu-genesys-usbhub-firmware.h"

static void
fu_genesys_usbhub_device_project_compatibility_matrix_func(void)
{
	const gchar *projects[] = {
	    "LNV_P34WD40_L10",
	    "LNV_P34WD40_L20",
	    "LNV_P34WD40_L30",
	};
	const gchar *mask_project_ic_types[] = {"352510", "352350", "352350"};

	for (guint device_idx = 0; device_idx < G_N_ELEMENTS(projects); device_idx++) {
		g_autoptr(GBytes) device_project =
		    g_bytes_new_static(projects[device_idx], strlen(projects[device_idx]));
		for (guint firmware_idx = 0; firmware_idx < G_N_ELEMENTS(projects);
		     firmware_idx++) {
			g_autoptr(GBytes) firmware_project =
			    g_bytes_new_static(projects[firmware_idx],
					       strlen(projects[firmware_idx]));
			g_autoptr(GError) error = NULL;
			gboolean ret = fu_genesys_usbhub_device_check_project_compatibility(
			    TRUE,
			    device_project,
			    mask_project_ic_types[device_idx],
			    firmware_project,
			    mask_project_ic_types[firmware_idx],
			    &error);

			if (device_idx == firmware_idx) {
				g_assert_no_error(error);
				g_assert_true(ret);
			} else {
				g_assert_error(error, FWUPD_ERROR, FWUPD_ERROR_INVALID_FILE);
				g_assert_false(ret);
			}
		}
	}
}

static void
fu_genesys_usbhub_device_project_compatibility_ic_mismatch_func(void)
{
	const gchar project[] = "LNV_P34WD40_L20";
	g_autoptr(GBytes) project_bytes = g_bytes_new_static(project, sizeof(project) - 1);
	g_autoptr(GError) error = NULL;
	gboolean ret = fu_genesys_usbhub_device_check_project_compatibility(TRUE,
									    project_bytes,
									    "352350",
									    project_bytes,
									    "352510",
									    &error);

	g_assert_error(error, FWUPD_ERROR, FWUPD_ERROR_INVALID_FILE);
	g_assert_false(ret);
}

static void
fu_genesys_usbhub_device_project_compatibility_missing_data_func(void)
{
	const gchar project[] = "LNV_P34WD40_L10";
	g_autoptr(GBytes) project_bytes = g_bytes_new_static(project, sizeof(project) - 1);
	g_autoptr(GBytes) short_project = g_bytes_new_static("LNV_P34WD40_L1", 14);
	g_autoptr(GError) error_device_project = NULL;
	g_autoptr(GError) error_firmware_project = NULL;
	g_autoptr(GError) error_device_ic = NULL;
	g_autoptr(GError) error_firmware_ic = NULL;
	g_autoptr(GError) error_short_project = NULL;
	gboolean ret;

	ret = fu_genesys_usbhub_device_check_project_compatibility(TRUE,
								   NULL,
								   "352510",
								   project_bytes,
								   "352510",
								   &error_device_project);
	g_assert_error(error_device_project, FWUPD_ERROR, FWUPD_ERROR_NOT_SUPPORTED);
	g_assert_false(ret);

	ret = fu_genesys_usbhub_device_check_project_compatibility(TRUE,
								   project_bytes,
								   "352510",
								   NULL,
								   "352510",
								   &error_firmware_project);
	g_assert_error(error_firmware_project, FWUPD_ERROR, FWUPD_ERROR_INVALID_FILE);
	g_assert_false(ret);

	ret = fu_genesys_usbhub_device_check_project_compatibility(TRUE,
								   project_bytes,
								   NULL,
								   project_bytes,
								   "352510",
								   &error_device_ic);
	g_assert_error(error_device_ic, FWUPD_ERROR, FWUPD_ERROR_NOT_SUPPORTED);
	g_assert_false(ret);

	ret = fu_genesys_usbhub_device_check_project_compatibility(TRUE,
								   project_bytes,
								   "352510",
								   project_bytes,
								   NULL,
								   &error_firmware_ic);
	g_assert_error(error_firmware_ic, FWUPD_ERROR, FWUPD_ERROR_INVALID_FILE);
	g_assert_false(ret);

	ret = fu_genesys_usbhub_device_check_project_compatibility(TRUE,
								   short_project,
								   "352510",
								   project_bytes,
								   "352510",
								   &error_short_project);
	g_assert_error(error_short_project, FWUPD_ERROR, FWUPD_ERROR_NOT_SUPPORTED);
	g_assert_false(ret);
}

static void
fu_genesys_usbhub_device_project_compatibility_non_strict_func(void)
{
	g_autoptr(GError) error = NULL;
	gboolean ret = fu_genesys_usbhub_device_check_project_compatibility(FALSE,
									    NULL,
									    NULL,
									    NULL,
									    NULL,
									    &error);

	g_assert_no_error(error);
	g_assert_true(ret);
}

static void
fu_genesys_usbhub_device_p34_hid_quirk_func(void)
{
	const gchar *hid_instance_ids[] = {
	    "USB\\VID_17EF&PID_F00F", /* L1 */
	    "USB\\VID_17EF&PID_4C41", /* L2 */
	    "USB\\VID_17EF&PID_4C42", /* L3 */
	};
	g_autofree gchar *quirks_dir = g_strdup(SRCDIR);
	g_autoptr(FuContext) ctx = g_object_new(FU_TYPE_CONTEXT, NULL);
	g_autoptr(FuProgress) progress = fu_progress_new(G_STRLOC);
	g_autoptr(GError) error = NULL;
	gboolean ret;

	/* the quirk GType is looked up by name, so the type must be registered */
	g_type_ensure(FU_TYPE_GENESYS_HUBHID_DEVICE);

	fu_context_set_path(ctx, FU_PATH_KIND_DATADIR_QUIRKS, quirks_dir);
	fu_context_add_flag(ctx, FU_CONTEXT_FLAG_NO_CACHE);
	ret = fu_context_load(ctx, progress, FU_CONTEXT_LOAD_FLAG_NONE, &error);
	g_assert_no_error(error);
	g_assert_true(ret);

	for (guint i = 0; i < G_N_ELEMENTS(hid_instance_ids); i++) {
		g_autoptr(FuDevice) device = g_object_new(FU_TYPE_GENESYS_USBHUB_DEVICE, NULL);
		fu_device_set_context(device, ctx);
		fu_device_add_instance_id(device, hid_instance_ids[i]);
		g_assert_cmpuint(fu_device_get_specialized_gtype(device),
				 ==,
				 FU_TYPE_GENESYS_HUBHID_DEVICE);
	}
}

/*
 * HID proxy safety gate. Expected values come from the verified P34WD-40 topology:
 * L1 1121<->f00f, L2 1122<->4c41, L3 1123<->4c42. No test here touches hardware.
 */
typedef struct {
	const gchar *hub_instance_id;
	guint16 hub_pid;
	guint16 hid_pid;
} FuGenesysTestPair;

static const FuGenesysTestPair fu_genesys_test_pairs[] = {
    {"USB\\VID_17EF&PID_1121", 0x1121, 0xF00F},
    {"USB\\VID_17EF&PID_1122", 0x1122, 0x4C41},
    {"USB\\VID_17EF&PID_1123", 0x1123, 0x4C42},
};

#define FU_GENESYS_TEST_HUB_PHYSICAL_ID	  "usb:01:04"
#define FU_GENESYS_TEST_OTHER_PHYSICAL_ID "usb:01:07"

static FuContext *
fu_genesys_test_context_new(void)
{
	g_autofree gchar *quirks_dir = g_strdup(SRCDIR);
	g_autoptr(FuContext) ctx = g_object_new(FU_TYPE_CONTEXT, NULL);
	g_autoptr(FuProgress) progress = fu_progress_new(G_STRLOC);
	g_autoptr(GError) error = NULL;
	gboolean ret;

	g_type_ensure(FU_TYPE_GENESYS_HUBHID_DEVICE);
	fu_context_add_quirk_key(ctx, "GenesysHidProxyPid");
	fu_context_add_quirk_key(ctx, "GenesysParentHubPid");
	fu_context_set_path(ctx, FU_PATH_KIND_DATADIR_QUIRKS, quirks_dir);
	fu_context_add_flag(ctx, FU_CONTEXT_FLAG_NO_CACHE);
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
	fu_device_set_physical_id(device, FU_GENESYS_TEST_HUB_PHYSICAL_ID);
	return device;
}

static FuDevice *
fu_genesys_test_hid_new(guint16 vid, guint16 pid)
{
	FuDevice *device = g_object_new(FU_TYPE_GENESYS_HUBHID_DEVICE, NULL);
	fu_device_set_vid(device, vid);
	fu_device_set_pid(device, pid);
	return device;
}

static void
fu_genesys_test_assert_gate_error(GError *error, FwupdError code)
{
	g_assert_error(error, FWUPD_ERROR, code);
	g_assert_nonnull(g_strstr_len(error->message, -1, "HID proxy"));
}

/* every entry that can change hardware state must refuse before sending any command */
static void
fu_genesys_test_assert_entries_refuse(FuDevice *device, /* nocheck:name */
				      FwupdError code)
{
	FuDeviceClass *klass = FU_DEVICE_GET_CLASS(device);
	g_autoptr(FuProgress) progress = fu_progress_new(G_STRLOC);
	g_autoptr(FuFirmware) firmware = fu_firmware_new();

	{
		g_autoptr(GError) error = NULL;
		g_assert_false(klass->detach(device, progress, &error));
		fu_genesys_test_assert_gate_error(error, code);
	}
	{
		g_autoptr(GError) error = NULL;
		g_assert_false(klass->prepare(device, progress, FWUPD_INSTALL_FLAG_NONE, &error));
		fu_genesys_test_assert_gate_error(error, code);
	}
	{
		g_autoptr(GError) error = NULL;
		g_assert_false(klass->write_firmware(device,
						     firmware,
						     progress,
						     FWUPD_INSTALL_FLAG_NONE,
						     &error));
		fu_genesys_test_assert_gate_error(error, code);
	}
	{
		g_autoptr(GError) error = NULL;
		g_autoptr(GBytes) blob = klass->dump_firmware(device, progress, &error);
		g_assert_null(blob);
		fu_genesys_test_assert_gate_error(error, code);
	}
}

/* acceptance 1: no proxy -> inhibited and every state-changing entry refuses */
static void
fu_genesys_usbhub_device_hid_proxy_missing_func(void)
{
	g_autoptr(FuContext) ctx = fu_genesys_test_context_new();

	for (guint i = 0; i < G_N_ELEMENTS(fu_genesys_test_pairs); i++) {
		g_autoptr(FuDevice) hub =
		    fu_genesys_test_hub_new(ctx,
					    fu_genesys_test_pairs[i].hub_instance_id,
					    fu_genesys_test_pairs[i].hub_pid);
		g_autoptr(GError) error = NULL;

		g_assert_true(
		    fu_device_has_private_flag(hub, FU_GENESYS_USBHUB_FLAG_REQUIRE_HID_PROXY));

		/* what setup does for a hub that needs the HID; init() already made it updatable */
		g_assert_true(fu_device_has_flag(hub, FWUPD_DEVICE_FLAG_UPDATABLE));
		fu_genesys_usbhub_device_refresh_hid_proxy_inhibit(FU_GENESYS_USBHUB_DEVICE(hub));
		g_assert_true(fu_device_has_inhibit(hub, "hid-proxy"));
		g_assert_false(fu_device_has_flag(hub, FWUPD_DEVICE_FLAG_UPDATABLE));
		g_assert_true(fu_device_has_flag(hub, FWUPD_DEVICE_FLAG_UPDATABLE_HIDDEN));

		g_assert_false(
		    fu_genesys_usbhub_device_verify_hid_proxy(FU_GENESYS_USBHUB_DEVICE(hub),
							      NULL,
							      NULL,
							      &error));
		fu_genesys_test_assert_gate_error(error, FWUPD_ERROR_NOT_FOUND);

		fu_genesys_test_assert_entries_refuse(hub, FWUPD_ERROR_NOT_FOUND);
	}
}

/* acceptance 2: the right HID bound to its own hub is allowed (decision logic, no hardware) */
static void
fu_genesys_usbhub_device_hid_proxy_correct_func(void)
{
	g_autoptr(FuContext) ctx = fu_genesys_test_context_new();

	for (guint i = 0; i < G_N_ELEMENTS(fu_genesys_test_pairs); i++) {
		g_autoptr(FuDevice) hub =
		    fu_genesys_test_hub_new(ctx,
					    fu_genesys_test_pairs[i].hub_instance_id,
					    fu_genesys_test_pairs[i].hub_pid);
		g_autoptr(FuDevice) hid =
		    fu_genesys_test_hid_new(0x17EF, fu_genesys_test_pairs[i].hid_pid);
		g_autoptr(GError) error = NULL;

		fu_genesys_usbhub_device_refresh_hid_proxy_inhibit(FU_GENESYS_USBHUB_DEVICE(hub));
		g_assert_true(fu_device_has_inhibit(hub, "hid-proxy"));

		g_assert_true(fu_genesys_usbhub_device_set_proxy(FU_GENESYS_USBHUB_DEVICE(hub),
								 hid,
								 FU_GENESYS_TEST_HUB_PHYSICAL_ID,
								 &error));
		g_assert_no_error(error);
		g_assert_true(fu_device_get_proxy(hub, NULL) == hid);
		g_assert_false(fu_device_has_inhibit(hub, "hid-proxy"));
		g_assert_true(fu_device_has_flag(hub, FWUPD_DEVICE_FLAG_UPDATABLE));
		g_assert_true(
		    fu_genesys_usbhub_device_verify_hid_proxy(FU_GENESYS_USBHUB_DEVICE(hub),
							      hid,
							      FU_GENESYS_TEST_HUB_PHYSICAL_ID,
							      &error));
		g_assert_no_error(error);

		/* a proxy whose parent cannot be resolved is not trusted (fail closed) */
		fu_genesys_test_assert_entries_refuse(hub, FWUPD_ERROR_INVALID_DATA);
	}
}

/* acceptance 3: a proxy that belongs to another hub, port or vendor is refused */
static void
fu_genesys_usbhub_device_hid_proxy_wrong_hub_func(void)
{
	g_autoptr(FuContext) ctx = fu_genesys_test_context_new();

	for (guint i = 0; i < G_N_ELEMENTS(fu_genesys_test_pairs); i++) {
		for (guint j = 0; j < G_N_ELEMENTS(fu_genesys_test_pairs); j++) {
			g_autoptr(FuDevice) hub =
			    fu_genesys_test_hub_new(ctx,
						    fu_genesys_test_pairs[i].hub_instance_id,
						    fu_genesys_test_pairs[i].hub_pid);
			g_autoptr(FuDevice) hid =
			    fu_genesys_test_hid_new(0x17EF, fu_genesys_test_pairs[j].hid_pid);
			g_autoptr(GError) error = NULL;
			gboolean ret;

			fu_genesys_usbhub_device_refresh_hid_proxy_inhibit(
			    FU_GENESYS_USBHUB_DEVICE(hub));
			ret = fu_genesys_usbhub_device_set_proxy(FU_GENESYS_USBHUB_DEVICE(hub),
								 hid,
								 FU_GENESYS_TEST_HUB_PHYSICAL_ID,
								 &error);
			if (i == j) {
				g_assert_no_error(error);
				g_assert_true(ret);
				continue;
			}
			/* only the diagonal pairs are valid */
			g_assert_error(error, FWUPD_ERROR, FWUPD_ERROR_INVALID_DATA);
			g_assert_false(ret);
			g_assert_null(fu_device_get_proxy(hub, NULL));
			g_assert_true(fu_device_has_inhibit(hub, "hid-proxy"));
		}
	}

	/* the right HID model, but enumerated below a different port */
	{
		g_autoptr(FuDevice) hub =
		    fu_genesys_test_hub_new(ctx,
					    fu_genesys_test_pairs[1].hub_instance_id,
					    fu_genesys_test_pairs[1].hub_pid);
		g_autoptr(FuDevice) hid = fu_genesys_test_hid_new(0x17EF, 0x4C41);
		g_autoptr(GError) error = NULL;

		fu_genesys_usbhub_device_refresh_hid_proxy_inhibit(FU_GENESYS_USBHUB_DEVICE(hub));
		g_assert_false(fu_genesys_usbhub_device_set_proxy(FU_GENESYS_USBHUB_DEVICE(hub),
								  hid,
								  FU_GENESYS_TEST_OTHER_PHYSICAL_ID,
								  &error));
		g_assert_error(error, FWUPD_ERROR, FWUPD_ERROR_INVALID_DATA);
		g_assert_null(fu_device_get_proxy(hub, NULL));
		g_assert_true(fu_device_has_inhibit(hub, "hid-proxy"));
	}

	/* the right model number from another vendor */
	{
		g_autoptr(FuDevice) hub =
		    fu_genesys_test_hub_new(ctx,
					    fu_genesys_test_pairs[2].hub_instance_id,
					    fu_genesys_test_pairs[2].hub_pid);
		g_autoptr(FuDevice) hid = fu_genesys_test_hid_new(0x05E3, 0x4C42);
		g_autoptr(GError) error = NULL;

		g_assert_false(fu_genesys_usbhub_device_set_proxy(FU_GENESYS_USBHUB_DEVICE(hub),
								  hid,
								  FU_GENESYS_TEST_HUB_PHYSICAL_ID,
								  &error));
		g_assert_error(error, FWUPD_ERROR, FWUPD_ERROR_INVALID_DATA);
	}

	/* an unresolvable parent or hub id cannot be verified, so it is refused */
	{
		g_autoptr(FuDevice) hub =
		    fu_genesys_test_hub_new(ctx,
					    fu_genesys_test_pairs[0].hub_instance_id,
					    fu_genesys_test_pairs[0].hub_pid);
		g_autoptr(FuDevice) hid = fu_genesys_test_hid_new(0x17EF, 0xF00F);
		g_autoptr(GError) error = NULL;

		g_assert_false(fu_genesys_usbhub_device_set_proxy(FU_GENESYS_USBHUB_DEVICE(hub),
								  hid,
								  NULL,
								  &error));
		g_assert_error(error, FWUPD_ERROR, FWUPD_ERROR_NOT_SUPPORTED);
		g_assert_null(fu_device_get_proxy(hub, NULL));
	}
}

/* acceptance 4: a late HID, a replaced hub and a moved HID are verified again */
static void
fu_genesys_usbhub_device_hid_proxy_replug_func(void)
{
	g_autoptr(FuContext) ctx = fu_genesys_test_context_new();
	g_autoptr(FuDevice) hub = fu_genesys_test_hub_new(ctx,
							  fu_genesys_test_pairs[2].hub_instance_id,
							  fu_genesys_test_pairs[2].hub_pid);
	g_autoptr(FuDevice) hid = fu_genesys_test_hid_new(0x17EF, 0x4C42);
	g_autoptr(GError) error = NULL;

	/* the hub is up before its HID: not allowed yet */
	fu_genesys_usbhub_device_refresh_hid_proxy_inhibit(FU_GENESYS_USBHUB_DEVICE(hub));
	g_assert_true(fu_device_has_inhibit(hub, "hid-proxy"));
	fu_genesys_test_assert_entries_refuse(hub, FWUPD_ERROR_NOT_FOUND);

	/* the HID shows up late: bound after verification only */
	g_assert_true(fu_genesys_usbhub_device_set_proxy(FU_GENESYS_USBHUB_DEVICE(hub),
							 hid,
							 FU_GENESYS_TEST_HUB_PHYSICAL_ID,
							 &error));
	g_assert_no_error(error);
	g_assert_false(fu_device_has_inhibit(hub, "hid-proxy"));

	/* the HID is re-enumerated below another port: verified again at the point of use */
	g_assert_false(fu_genesys_usbhub_device_verify_hid_proxy(FU_GENESYS_USBHUB_DEVICE(hub),
								 hid,
								 FU_GENESYS_TEST_OTHER_PHYSICAL_ID,
								 &error));
	fu_genesys_test_assert_gate_error(error, FWUPD_ERROR_INVALID_DATA);
	g_clear_error(&error);

	/* the hub is replugged: the new object starts inhibited without the old proxy */
	{
		g_autoptr(FuDevice) hub_new =
		    fu_genesys_test_hub_new(ctx,
					    fu_genesys_test_pairs[2].hub_instance_id,
					    fu_genesys_test_pairs[2].hub_pid);
		fu_device_replace(hub_new, hub);
		fu_genesys_usbhub_device_refresh_hid_proxy_inhibit(
		    FU_GENESYS_USBHUB_DEVICE(hub_new));
		g_assert_null(fu_device_get_proxy(hub_new, NULL));
		g_assert_true(fu_device_has_inhibit(hub_new, "hid-proxy"));
		g_assert_false(fu_device_has_flag(hub_new, FWUPD_DEVICE_FLAG_UPDATABLE));
		fu_genesys_test_assert_entries_refuse(hub_new, FWUPD_ERROR_NOT_FOUND);

		/* the HID is bound to the new hub again only through verification */
		g_assert_false(fu_genesys_usbhub_device_set_proxy(FU_GENESYS_USBHUB_DEVICE(hub_new),
								  hid,
								  FU_GENESYS_TEST_OTHER_PHYSICAL_ID,
								  &error));
		g_assert_error(error, FWUPD_ERROR, FWUPD_ERROR_INVALID_DATA);
		g_clear_error(&error);
		g_assert_true(fu_genesys_usbhub_device_set_proxy(FU_GENESYS_USBHUB_DEVICE(hub_new),
								 hid,
								 FU_GENESYS_TEST_HUB_PHYSICAL_ID,
								 &error));
		g_assert_no_error(error);
		g_assert_false(fu_device_has_inhibit(hub_new, "hid-proxy"));
	}
}

/* acceptance 5: hubs that do not ask for the gate are unchanged */
static void
fu_genesys_usbhub_device_hid_proxy_no_regression_func(void)
{
	g_autoptr(FuContext) ctx = fu_genesys_test_context_new();
	g_autoptr(FuDevice) hub = fu_genesys_test_hub_new(ctx, "USB\\VID_17EF&PID_1159", 0x1159);
	g_autoptr(FuDevice) hid = fu_genesys_test_hid_new(0x05E3, 0x0102);
	g_autoptr(GError) error = NULL;

	/* a hub without the flag is never inhibited and never verified */
	g_assert_false(fu_device_has_private_flag(hub, FU_GENESYS_USBHUB_FLAG_REQUIRE_HID_PROXY));
	fu_genesys_usbhub_device_refresh_hid_proxy_inhibit(FU_GENESYS_USBHUB_DEVICE(hub));
	g_assert_false(fu_device_has_inhibit(hub, "hid-proxy"));
	g_assert_true(fu_genesys_usbhub_device_verify_hid_proxy(FU_GENESYS_USBHUB_DEVICE(hub),
								NULL,
								NULL,
								&error));
	g_assert_no_error(error);
	g_assert_true(fu_genesys_usbhub_device_set_proxy(FU_GENESYS_USBHUB_DEVICE(hub),
							 hid,
							 "usb:unrelated",
							 &error));
	g_assert_no_error(error);
	g_assert_true(fu_device_get_proxy(hub, NULL) == hid);

	/* the hubs of the dock all ask for the gate */
	for (guint i = 0; i < G_N_ELEMENTS(fu_genesys_test_pairs); i++) {
		g_autoptr(FuDevice) dock =
		    fu_genesys_test_hub_new(ctx,
					    fu_genesys_test_pairs[i].hub_instance_id,
					    fu_genesys_test_pairs[i].hub_pid);
		g_assert_true(
		    fu_device_has_private_flag(dock, FU_GENESYS_USBHUB_FLAG_REQUIRE_HID_PROXY));
		g_assert_true(
		    fu_device_has_private_flag(dock, FU_GENESYS_USBHUB_FLAG_STRICT_PROJECT_CHECK));
		g_assert_true(fu_device_has_flag(dock, FWUPD_DEVICE_FLAG_DUAL_IMAGE));
		g_assert_true(
		    fu_device_has_private_flag(dock, FU_GENESYS_USBHUB_FLAG_HID_TRANSPORT));
	}
}

/*
 * Offline identity tests. Every PROJECT string, IC type and PID below is synthetic test data,
 * not a statement about real hardware. Only an exact PROJECT + mask IC match is accepted,
 * anything else, including unknown identity, is refused.
 */
typedef struct {
	const gchar *project;
	const gchar *ic;
} FuGenesysTestIdentity;

static const FuGenesysTestIdentity fu_genesys_test_identities[] = {
    /* read from the P34 hub dumps and vendor images: L20 and L30 share one IC type */
    {"LNV_P34WD40_L10", "352510"},
    {"LNV_P34WD40_L20", "352350"},
    {"LNV_P34WD40_L30", "352350"},
    /* synthetic: two models share one IC type, so the PROJECT string alone must separate them */
    {"SYN_COLLIDE_A10", "999001"},
    {"SYN_COLLIDE_B10", "999001"},
};

/* no two identities accept each other's firmware, including identities that share an IC type */
static void
fu_genesys_usbhub_device_identity_cross_model_func(void)
{
	for (guint d = 0; d < G_N_ELEMENTS(fu_genesys_test_identities); d++) {
		g_autoptr(GBytes) dev =
		    g_bytes_new_static(fu_genesys_test_identities[d].project,
				       strlen(fu_genesys_test_identities[d].project));
		for (guint f = 0; f < G_N_ELEMENTS(fu_genesys_test_identities); f++) {
			g_autoptr(GBytes) fw =
			    g_bytes_new_static(fu_genesys_test_identities[f].project,
					       strlen(fu_genesys_test_identities[f].project));
			g_autoptr(GError) error = NULL;
			gboolean ret = fu_genesys_usbhub_device_check_project_compatibility(
			    TRUE,
			    dev,
			    fu_genesys_test_identities[d].ic,
			    fw,
			    fu_genesys_test_identities[f].ic,
			    &error);
			if (d == f) {
				g_assert_no_error(error);
				g_assert_true(ret);
			} else {
				g_assert_error(error, FWUPD_ERROR, FWUPD_ERROR_INVALID_FILE);
				g_assert_false(ret);
			}
		}
	}

	/* same PROJECT, other mask IC: refused; same mask IC, other PROJECT: refused */
	{
		g_autoptr(GBytes) project = g_bytes_new_static("LNV_P34WD40_L20", 15);
		g_autoptr(GBytes) other = g_bytes_new_static("LNV_P34WD40_L30", 15);
		g_autoptr(GError) error_ic = NULL;
		g_autoptr(GError) error_project = NULL;

		g_assert_false(fu_genesys_usbhub_device_check_project_compatibility(TRUE,
										    project,
										    "352360",
										    project,
										    "352350",
										    &error_ic));
		g_assert_error(error_ic, FWUPD_ERROR, FWUPD_ERROR_INVALID_FILE);
		g_assert_false(
		    fu_genesys_usbhub_device_check_project_compatibility(TRUE,
									 project,
									 "352350",
									 other,
									 "352350",
									 &error_project));
		g_assert_error(error_project, FWUPD_ERROR, FWUPD_ERROR_INVALID_FILE);
	}
}

/* unknown or malformed identity is refused on both sides; there is no fallback to a looser match */
static void
fu_genesys_usbhub_device_identity_unknown_identity_func(void)
{
	const gsize lengths[] = {0, 1, 14, 16, 64};
	g_autoptr(GBytes) good = g_bytes_new_static("LNV_P34WD40_L20", 15);
	g_autoptr(GError) error_ok = NULL;

	/* control: identical identities are accepted, so refusals below are not fixture noise */
	g_assert_true(fu_genesys_usbhub_device_check_project_compatibility(TRUE,
									   good,
									   "352360",
									   good,
									   "352360",
									   &error_ok));
	g_assert_no_error(error_ok);

	for (guint i = 0; i < G_N_ELEMENTS(lengths); i++) {
		g_autofree guint8 *buf = g_malloc(lengths[i] + 1);
		g_autoptr(GBytes) odd = NULL;
		g_autoptr(GError) error_dev = NULL;
		g_autoptr(GError) error_fw = NULL;

		/* g_bytes_new() copies its input: fill the buffer first */
		memset(buf, 'X', lengths[i] + 1);
		odd = g_bytes_new(buf, lengths[i]);
		g_assert_cmpuint(g_bytes_get_size(odd), ==, lengths[i]);

		g_assert_false(fu_genesys_usbhub_device_check_project_compatibility(TRUE,
										    odd,
										    "352360",
										    good,
										    "352360",
										    &error_dev));
		g_assert_error(error_dev, FWUPD_ERROR, FWUPD_ERROR_NOT_SUPPORTED);
		g_assert_false(fu_genesys_usbhub_device_check_project_compatibility(TRUE,
										    good,
										    "352360",
										    odd,
										    "352360",
										    &error_fw));
		g_assert_error(error_fw, FWUPD_ERROR, FWUPD_ERROR_INVALID_FILE);
	}

	/* mask IC type that is not six characters */
	{
		const gchar *bad_ics[] = {"", "35236", "3523600"};
		for (guint i = 0; i < G_N_ELEMENTS(bad_ics); i++) {
			g_autoptr(GError) error_dev = NULL;
			g_autoptr(GError) error_fw = NULL;
			g_assert_false(
			    fu_genesys_usbhub_device_check_project_compatibility(TRUE,
										 good,
										 bad_ics[i],
										 good,
										 "352360",
										 &error_dev));
			g_assert_error(error_dev, FWUPD_ERROR, FWUPD_ERROR_NOT_SUPPORTED);
			g_assert_false(
			    fu_genesys_usbhub_device_check_project_compatibility(TRUE,
										 good,
										 "352360",
										 good,
										 bad_ics[i],
										 &error_fw));
			g_assert_error(error_fw, FWUPD_ERROR, FWUPD_ERROR_INVALID_FILE);
		}
	}
}

/* the strict check is the flag, and no other flag combination relaxes it in check_firmware() */
static void
fu_genesys_usbhub_device_identity_check_firmware_not_relaxed_func(void)
{
	const gchar *others[] = {
	    FU_GENESYS_USBHUB_FLAG_HID_TRANSPORT,
	    FU_GENESYS_USBHUB_FLAG_REQUIRE_HID_PROXY,
	};

	for (guint mask = 0; mask < (1u << G_N_ELEMENTS(others)); mask++) {
		g_autoptr(FuDevice) device = g_object_new(FU_TYPE_GENESYS_USBHUB_DEVICE, NULL);
		g_autoptr(FuFirmware) firmware = fu_firmware_new();
		g_autoptr(FuFirmware) genesys_fw =
		    g_object_new(FU_TYPE_GENESYS_USBHUB_FIRMWARE, NULL);
		g_autoptr(GError) error = NULL;
		g_autoptr(GError) error_genesys = NULL;
		FuDeviceClass *klass = FU_DEVICE_GET_CLASS(device);

		fu_device_add_private_flag(device, FU_GENESYS_USBHUB_FLAG_STRICT_PROJECT_CHECK);
		for (guint i = 0; i < G_N_ELEMENTS(others); i++) {
			if (mask & (1u << i))
				fu_device_add_private_flag(device, others[i]);
		}

		/* not a Genesys hub firmware, and the device exposes no identity at all */
		g_assert_nonnull(klass->check_firmware);
		g_assert_false(
		    klass->check_firmware(device, firmware, FU_FIRMWARE_PARSE_FLAG_NONE, &error));
		g_assert_error(error, FWUPD_ERROR, FWUPD_ERROR_INVALID_FILE);

		/* a Genesys hub firmware without device identity is refused (fail closed) */
		g_assert_false(klass->check_firmware(device,
						     genesys_fw,
						     FU_FIRMWARE_PARSE_FLAG_NONE,
						     &error_genesys));
		g_assert_error(error_genesys, FWUPD_ERROR, FWUPD_ERROR_NOT_SUPPORTED);
	}
}

static FuDevice *
fu_genesys_test_synthetic_hub_new(FuContext *ctx,
				  const gchar *physical_id,
				  guint16 pid,
				  const gchar *hid_pid)
{
	g_autoptr(GError) error = NULL;
	FuDevice *hub = g_object_new(FU_TYPE_GENESYS_USBHUB_DEVICE, NULL);

	fu_device_set_context(hub, ctx);
	fu_device_set_vid(hub, 0x17EF);
	fu_device_set_pid(hub, pid);
	fu_device_set_physical_id(hub, physical_id);
	fu_device_add_private_flag(hub, FU_GENESYS_USBHUB_FLAG_REQUIRE_HID_PROXY);
	g_assert_true(fu_device_set_quirk_kv(hub,
					     "GenesysHidProxyPid",
					     hid_pid,
					     FU_CONTEXT_QUIRK_SOURCE_DB,
					     &error));
	g_assert_no_error(error);
	return hub;
}

/*
 * The HID VID/PIDs 17ef:f00f, 17ef:4c41 and 17ef:4c42 are shared between models. A HID may only
 * become the update channel of the hub it hangs below, never of another dock's hub or of a hub
 * that expects another HID model, even when the VID/PID looks right.
 */
static void
fu_genesys_usbhub_device_identity_shared_hid_own_dock_func(void)
{
	g_autoptr(FuContext) ctx = fu_genesys_test_context_new();
	/* two docks, both with a hub that expects the shared 4c41 HID */
	g_autoptr(FuDevice) hub_a =
	    fu_genesys_test_synthetic_hub_new(ctx, "usb:01:04", 0x9901, "0x4C41");
	g_autoptr(FuDevice) hub_b =
	    fu_genesys_test_synthetic_hub_new(ctx, "usb:02:04", 0x9901, "0x4C41");
	g_autoptr(FuDevice) hid_a = fu_genesys_test_hid_new(0x17EF, 0x4C41);
	g_autoptr(FuDevice) hid_b = fu_genesys_test_hid_new(0x17EF, 0x4C41);
	g_autoptr(GError) error = NULL;

	/* each dock binds its own HID */
	g_assert_true(fu_genesys_usbhub_device_set_proxy(FU_GENESYS_USBHUB_DEVICE(hub_a),
							 hid_a,
							 "usb:01:04",
							 &error));
	g_assert_no_error(error);
	g_assert_true(fu_genesys_usbhub_device_set_proxy(FU_GENESYS_USBHUB_DEVICE(hub_b),
							 hid_b,
							 "usb:02:04",
							 &error));
	g_assert_no_error(error);
	g_assert_true(fu_device_get_proxy(hub_a, NULL) == hid_a);
	g_assert_true(fu_device_get_proxy(hub_b, NULL) == hid_b);
}

static void
fu_genesys_usbhub_device_identity_shared_hid_refused_func(void)
{
	g_autoptr(FuContext) ctx = fu_genesys_test_context_new();
	g_autoptr(FuDevice) hub_a =
	    fu_genesys_test_synthetic_hub_new(ctx, "usb:01:04", 0x9901, "0x4C41");
	g_autoptr(FuDevice) hub_other =
	    fu_genesys_test_synthetic_hub_new(ctx, "usb:03:04", 0x9902, "0x4C42");
	g_autoptr(FuDevice) hub_unconfigured =
	    fu_genesys_test_synthetic_hub_new(ctx, "usb:04:04", 0x9903, "0x0");
	g_autoptr(FuDevice) hid_4c41_dock_b = fu_genesys_test_hid_new(0x17EF, 0x4C41);
	g_autoptr(FuDevice) hid_4c41 = fu_genesys_test_hid_new(0x17EF, 0x4C41);
	g_autoptr(GError) error = NULL;

	/* dock B's 4c41 HID is never bound to dock A's hub */
	g_assert_false(fu_genesys_usbhub_device_set_proxy(FU_GENESYS_USBHUB_DEVICE(hub_a),
							  hid_4c41_dock_b,
							  "usb:02:04",
							  &error));
	g_assert_error(error, FWUPD_ERROR, FWUPD_ERROR_INVALID_DATA);
	g_assert_null(fu_device_get_proxy(hub_a, NULL));
	g_clear_error(&error);

	/* a hub of another model, same shared VID/PID family but a different expected PID */
	g_assert_false(fu_genesys_usbhub_device_set_proxy(FU_GENESYS_USBHUB_DEVICE(hub_other),
							  hid_4c41,
							  "usb:03:04",
							  &error));
	g_assert_error(error, FWUPD_ERROR, FWUPD_ERROR_INVALID_DATA);
	g_assert_null(fu_device_get_proxy(hub_other, NULL));
	g_clear_error(&error);

	/* no configured HID model: nothing can be bound, even the right-looking HID */
	g_assert_false(
	    fu_genesys_usbhub_device_set_proxy(FU_GENESYS_USBHUB_DEVICE(hub_unconfigured),
					       hid_4c41,
					       "usb:04:04",
					       &error));
	g_assert_error(error, FWUPD_ERROR, FWUPD_ERROR_NOT_SUPPORTED);
	g_assert_null(fu_device_get_proxy(hub_unconfigured, NULL));
	g_clear_error(&error);

	/* a hub that expects 4c41 (1122) never takes a 4c41 HID that sits below another dock */
	{
		g_autoptr(FuContext) ctx_dock = fu_genesys_test_context_new();
		g_autoptr(FuDevice) dock =
		    fu_genesys_test_hub_new(ctx_dock, "USB\\VID_17EF&PID_1122", 0x1122);

		g_assert_false(fu_genesys_usbhub_device_set_proxy(FU_GENESYS_USBHUB_DEVICE(dock),
								  hid_4c41,
								  FU_GENESYS_TEST_OTHER_PHYSICAL_ID,
								  &error));
		g_assert_error(error, FWUPD_ERROR, FWUPD_ERROR_INVALID_DATA);
		g_assert_null(fu_device_get_proxy(dock, NULL));
	}
}

/*
 * P34WD-40 USB2.0 hub quirks. Expected values come from the topology read on a dock
 * (1121 > 1122 > 1123 with f00f / 4c41 / 4c42 below).
 */
static void
fu_genesys_usbhub_device_p34_quirk_func(void)
{
	const FuGenesysTestPair p34[] = {
	    {"USB\\VID_17EF&PID_1121", 0x1121, 0xF00F},
	    {"USB\\VID_17EF&PID_1122", 0x1122, 0x4C41},
	    {"USB\\VID_17EF&PID_1123", 0x1123, 0x4C42},
	};
	const gchar *unclaimed[] = {
	    "USB\\VID_17EF&PID_1124", /* USB3 side, out of scope */
	    "USB\\VID_17EF&PID_1125",
	    "USB\\VID_17EF&PID_1126",
	    "USB\\VID_17EF&PID_1159", /* no quirk */
	};
	g_autoptr(FuContext) ctx = fu_genesys_test_context_new();

	for (guint i = 0; i < G_N_ELEMENTS(p34); i++) {
		g_autoptr(FuDevice) hub =
		    fu_genesys_test_hub_new(ctx, p34[i].hub_instance_id, p34[i].hub_pid);
		g_autoptr(FuFirmware) genesys_fw =
		    g_object_new(FU_TYPE_GENESYS_USBHUB_FIRMWARE, NULL);
		g_autoptr(FuFirmware) other_fw = fu_firmware_new();
		g_autoptr(GError) error = NULL;
		FuDeviceClass *klass = FU_DEVICE_GET_CLASS(hub);

		g_assert_cmpuint(fu_device_get_specialized_gtype(hub),
				 ==,
				 FU_TYPE_GENESYS_USBHUB_DEVICE);
		g_assert_true(
		    fu_device_has_private_flag(hub, FU_GENESYS_USBHUB_FLAG_STRICT_PROJECT_CHECK));
		g_assert_true(
		    fu_device_has_private_flag(hub, FU_GENESYS_USBHUB_FLAG_REQUIRE_HID_PROXY));

		g_assert_true(
		    fu_device_has_private_flag(hub, FU_GENESYS_USBHUB_FLAG_SINGLE_HID_PROXY));

		/* hid-transport, dual-image and the 40 s replug window apply to all three */
		g_assert_true(
		    fu_device_has_private_flag(hub, FU_GENESYS_USBHUB_FLAG_HID_TRANSPORT));
		g_assert_true(fu_device_has_flag(hub, FWUPD_DEVICE_FLAG_DUAL_IMAGE));
		g_assert_cmpuint(fu_device_get_remove_delay(hub), ==, 40000);

		/* the strict check applies: a non-Genesys image is refused outright */
		g_assert_nonnull(klass->check_firmware);
		g_assert_false(
		    klass->check_firmware(hub, other_fw, FU_FIRMWARE_PARSE_FLAG_NONE, &error));
		g_assert_error(error, FWUPD_ERROR, FWUPD_ERROR_INVALID_FILE);
		g_clear_error(&error);

		/* ...and a hub with no readable identity refuses a Genesys image (fail closed) */
		g_assert_false(
		    klass->check_firmware(hub, genesys_fw, FU_FIRMWARE_PARSE_FLAG_NONE, &error));
		g_assert_error(error, FWUPD_ERROR, FWUPD_ERROR_NOT_SUPPORTED);
	}

	/* each hub takes only its own HID model, never the one of its neighbour */
	for (guint i = 0; i < G_N_ELEMENTS(p34); i++) {
		for (guint j = 0; j < G_N_ELEMENTS(p34); j++) {
			g_autoptr(FuDevice) hub =
			    fu_genesys_test_hub_new(ctx, p34[i].hub_instance_id, p34[i].hub_pid);
			g_autoptr(FuDevice) hid = fu_genesys_test_hid_new(0x17EF, p34[j].hid_pid);
			g_autoptr(GError) error = NULL;
			gboolean ret =
			    fu_genesys_usbhub_device_set_proxy(FU_GENESYS_USBHUB_DEVICE(hub),
							       hid,
							       FU_GENESYS_TEST_HUB_PHYSICAL_ID,
							       &error);

			if (i == j) {
				g_assert_no_error(error);
				g_assert_true(ret);
				g_assert_true(fu_device_get_proxy(hub, NULL) == hid);
			} else {
				g_assert_error(error, FWUPD_ERROR, FWUPD_ERROR_INVALID_DATA);
				g_assert_false(ret);
				g_assert_null(fu_device_get_proxy(hub, NULL));
			}
		}
	}

	for (guint i = 0; i < G_N_ELEMENTS(unclaimed); i++) {
		g_autoptr(FuDevice) device = g_object_new(FU_TYPE_GENESYS_USBHUB_DEVICE, NULL);
		fu_device_set_context(device, ctx);
		fu_device_add_instance_id(device, unclaimed[i]);
		g_assert_cmpuint(fu_device_get_specialized_gtype(device), ==, G_TYPE_INVALID);
		g_assert_false(
		    fu_device_has_private_flag(device,
					       FU_GENESYS_USBHUB_FLAG_STRICT_PROJECT_CHECK));
		g_assert_false(
		    fu_device_has_private_flag(device, FU_GENESYS_USBHUB_FLAG_REQUIRE_HID_PROXY));
		g_assert_false(
		    fu_device_has_private_flag(device, FU_GENESYS_USBHUB_FLAG_SINGLE_HID_PROXY));
	}
}

/* an erased identity that is identical on both sides must not pass the strict check */
static void
fu_genesys_usbhub_device_p34_blank_identity_func(void)
{
	const guint8 zeros[15] = {0};
	const guint8 ones[15] = {0xFF,
				 0xFF,
				 0xFF,
				 0xFF,
				 0xFF,
				 0xFF,
				 0xFF,
				 0xFF,
				 0xFF,
				 0xFF,
				 0xFF,
				 0xFF,
				 0xFF,
				 0xFF,
				 0xFF};
	const gchar *blank_masks[] = {"      ", "\xff\xff\xff\xff\xff\xff", "------"};
	g_autoptr(GBytes) good = g_bytes_new_static("LNV_P34WD40_L10", 15);
	g_autoptr(GBytes) blank_zeros = g_bytes_new_static(zeros, sizeof(zeros));
	g_autoptr(GBytes) blank_ones = g_bytes_new_static(ones, sizeof(ones));
	g_autoptr(GBytes) blank_spaces = g_bytes_new_static("               ", 15);
	GBytes *blank_projects[] = {blank_zeros, blank_ones, blank_spaces};

	for (guint i = 0; i < G_N_ELEMENTS(blank_projects); i++) {
		g_autoptr(GError) error_dev = NULL;
		g_autoptr(GError) error_fw = NULL;

		/* identical blank PROJECT on both sides */
		g_assert_false(
		    fu_genesys_usbhub_device_check_project_compatibility(TRUE,
									 blank_projects[i],
									 "352510",
									 blank_projects[i],
									 "352510",
									 &error_dev));
		g_assert_error(error_dev, FWUPD_ERROR, FWUPD_ERROR_NOT_SUPPORTED);

		/* blank firmware PROJECT against a good device */
		g_assert_false(
		    fu_genesys_usbhub_device_check_project_compatibility(TRUE,
									 good,
									 "352510",
									 blank_projects[i],
									 "352510",
									 &error_fw));
		g_assert_error(error_fw, FWUPD_ERROR, FWUPD_ERROR_INVALID_FILE);
	}
	for (guint i = 0; i < G_N_ELEMENTS(blank_masks); i++) {
		g_autoptr(GError) error_dev = NULL;
		g_autoptr(GError) error_fw = NULL;

		g_assert_false(fu_genesys_usbhub_device_check_project_compatibility(TRUE,
										    good,
										    blank_masks[i],
										    good,
										    blank_masks[i],
										    &error_dev));
		g_assert_error(error_dev, FWUPD_ERROR, FWUPD_ERROR_NOT_SUPPORTED);
		g_assert_false(fu_genesys_usbhub_device_check_project_compatibility(TRUE,
										    good,
										    "352510",
										    good,
										    blank_masks[i],
										    &error_fw));
		g_assert_error(error_fw, FWUPD_ERROR, FWUPD_ERROR_INVALID_FILE);
	}

	/* control: a real identity still passes */
	g_assert_true(fu_genesys_usbhub_device_check_project_compatibility(TRUE,
									   good,
									   "352510",
									   good,
									   "352510",
									   NULL));
}

/* several HID candidates below one hub: a wrong one never displaces a bound proxy */
static void
fu_genesys_usbhub_device_p34_hid_candidates_func(void)
{
	g_autoptr(FuContext) ctx = fu_genesys_test_context_new();
	g_autoptr(FuDevice) hub = fu_genesys_test_hub_new(ctx, "USB\\VID_17EF&PID_1122", 0x1122);
	g_autoptr(FuDevice) hid_ok = fu_genesys_test_hid_new(0x17EF, 0x4C41);
	g_autoptr(FuDevice) hid_wrong_pid = fu_genesys_test_hid_new(0x17EF, 0x4C42);
	g_autoptr(FuDevice) hid_wrong_vid = fu_genesys_test_hid_new(0x05E3, 0x4C41);
	g_autoptr(GError) error = NULL;

	g_assert_true(fu_genesys_usbhub_device_set_proxy(FU_GENESYS_USBHUB_DEVICE(hub),
							 hid_ok,
							 FU_GENESYS_TEST_HUB_PHYSICAL_ID,
							 &error));
	g_assert_no_error(error);

	g_assert_false(fu_genesys_usbhub_device_set_proxy(FU_GENESYS_USBHUB_DEVICE(hub),
							  hid_wrong_pid,
							  FU_GENESYS_TEST_HUB_PHYSICAL_ID,
							  &error));
	g_assert_error(error, FWUPD_ERROR, FWUPD_ERROR_INVALID_DATA);
	g_clear_error(&error);
	g_assert_true(fu_device_get_proxy(hub, NULL) == hid_ok);

	g_assert_false(fu_genesys_usbhub_device_set_proxy(FU_GENESYS_USBHUB_DEVICE(hub),
							  hid_wrong_vid,
							  FU_GENESYS_TEST_HUB_PHYSICAL_ID,
							  &error));
	g_assert_error(error, FWUPD_ERROR, FWUPD_ERROR_INVALID_DATA);
	g_clear_error(&error);
	g_assert_true(fu_device_get_proxy(hub, NULL) == hid_ok);
	g_assert_false(fu_device_has_inhibit(hub, "hid-proxy"));
}

/* a removed HID is replaced by the new one, physical_id never decides */
static void
fu_genesys_usbhub_device_p34_single_hid_replug_func(void)
{
	g_autoptr(FuContext) ctx = fu_genesys_test_context_new();
	g_autoptr(FuDevice) hub = fu_genesys_test_hub_new(ctx, "USB\\VID_17EF&PID_1122", 0x1122);
	g_autoptr(FuDevice) hid_old = fu_genesys_test_hid_new(0x17EF, 0x4C41);
	g_autoptr(FuDevice) hid_new = fu_genesys_test_hid_new(0x17EF, 0x4C41);
	g_autoptr(FuDevice) hid_wrong = fu_genesys_test_hid_new(0x17EF, 0x4C42);
	g_autoptr(GError) error = NULL;

	/* same physical_id on both: it is not a decision input */
	fu_device_set_physical_id(hid_old, "usb:01:05");
	fu_device_set_physical_id(hid_new, "usb:01:05");

	g_assert_true(fu_genesys_usbhub_device_set_proxy(FU_GENESYS_USBHUB_DEVICE(hub),
							 hid_old,
							 FU_GENESYS_TEST_HUB_PHYSICAL_ID,
							 &error));
	g_assert_no_error(error);
	g_assert_true(fu_device_get_proxy(hub, NULL) == hid_old);

	/* re-adding the bound object is not a second candidate */
	g_assert_true(fu_genesys_usbhub_device_set_proxy(FU_GENESYS_USBHUB_DEVICE(hub),
							 hid_old,
							 FU_GENESYS_TEST_HUB_PHYSICAL_ID,
							 &error));
	g_assert_no_error(error);

	/* the device list marks a removed device: the stale proxy is never used */
	fu_device_add_private_flag(hid_old, FU_DEVICE_PRIVATE_FLAG_UNCONNECTED);
	fu_genesys_test_assert_entries_refuse(hub, FWUPD_ERROR_NOT_FOUND);

	/* a wrong HID does not take over, even though the bound one is stale */
	g_assert_false(fu_genesys_usbhub_device_set_proxy(FU_GENESYS_USBHUB_DEVICE(hub),
							  hid_wrong,
							  FU_GENESYS_TEST_HUB_PHYSICAL_ID,
							  &error));
	g_assert_error(error, FWUPD_ERROR, FWUPD_ERROR_INVALID_DATA);
	g_clear_error(&error);
	g_assert_true(fu_device_get_proxy(hub, NULL) == hid_old);
	fu_genesys_test_assert_entries_refuse(hub, FWUPD_ERROR_NOT_FOUND);

	/* the verified replacement binds, the hub is not ambiguous */
	g_assert_true(fu_genesys_usbhub_device_set_proxy(FU_GENESYS_USBHUB_DEVICE(hub),
							 hid_new,
							 FU_GENESYS_TEST_HUB_PHYSICAL_ID,
							 &error));
	g_assert_no_error(error);
	g_assert_true(fu_device_get_proxy(hub, NULL) == hid_new);
	g_assert_false(fu_device_has_inhibit(hub, "hid-proxy"));
	g_assert_false(fu_device_has_inhibit(hub, "hid-proxy-ambiguous"));
}

/* two live HIDs for one hub: no unique proxy, fail closed until the hub is replugged */
static void
fu_genesys_usbhub_device_p34_single_hid_ambiguous_func(void)
{
	g_autoptr(FuContext) ctx = fu_genesys_test_context_new();
	g_autoptr(FuDevice) hub = fu_genesys_test_hub_new(ctx, "USB\\VID_17EF&PID_1123", 0x1123);
	g_autoptr(FuDevice) hid_a = fu_genesys_test_hid_new(0x17EF, 0x4C42);
	g_autoptr(FuDevice) hid_b = fu_genesys_test_hid_new(0x17EF, 0x4C42);
	g_autoptr(FuDevice) hid_wrong = fu_genesys_test_hid_new(0x17EF, 0x4C41);
	g_autoptr(GError) error = NULL;

	fu_device_set_physical_id(hid_a, "usb:01:04");
	fu_device_set_physical_id(hid_b, "usb:01:04");

	g_assert_true(fu_genesys_usbhub_device_set_proxy(FU_GENESYS_USBHUB_DEVICE(hub),
							 hid_a,
							 FU_GENESYS_TEST_HUB_PHYSICAL_ID,
							 &error));
	g_assert_no_error(error);

	/* a wrong candidate is only rejected, it makes nothing ambiguous */
	g_assert_false(fu_genesys_usbhub_device_set_proxy(FU_GENESYS_USBHUB_DEVICE(hub),
							  hid_wrong,
							  FU_GENESYS_TEST_HUB_PHYSICAL_ID,
							  &error));
	g_assert_error(error, FWUPD_ERROR, FWUPD_ERROR_INVALID_DATA);
	g_clear_error(&error);
	g_assert_true(fu_device_get_proxy(hub, NULL) == hid_a);
	g_assert_false(fu_device_has_inhibit(hub, "hid-proxy-ambiguous"));

	/* a second verified candidate while the first is live: nothing stays bound */
	g_assert_false(fu_genesys_usbhub_device_set_proxy(FU_GENESYS_USBHUB_DEVICE(hub),
							  hid_b,
							  FU_GENESYS_TEST_HUB_PHYSICAL_ID,
							  &error));
	g_assert_error(error, FWUPD_ERROR, FWUPD_ERROR_INVALID_DATA);
	g_assert_nonnull(g_strstr_len(error->message, -1, "ambiguous"));
	g_clear_error(&error);
	g_assert_null(fu_device_get_proxy(hub, NULL));
	g_assert_true(fu_device_has_inhibit(hub, "hid-proxy-ambiguous"));
	g_assert_true(fu_device_has_inhibit(hub, "hid-proxy"));
	g_assert_false(fu_device_has_flag(hub, FWUPD_DEVICE_FLAG_UPDATABLE));
	fu_genesys_test_assert_entries_refuse(hub, FWUPD_ERROR_INVALID_DATA);

	/* sticky: a late removal of the first HID does not make the second one trusted */
	fu_device_add_private_flag(hid_a, FU_DEVICE_PRIVATE_FLAG_UNCONNECTED);
	g_assert_false(fu_genesys_usbhub_device_set_proxy(FU_GENESYS_USBHUB_DEVICE(hub),
							  hid_b,
							  FU_GENESYS_TEST_HUB_PHYSICAL_ID,
							  &error));
	g_assert_error(error, FWUPD_ERROR, FWUPD_ERROR_INVALID_DATA);
	g_clear_error(&error);
	g_assert_null(fu_device_get_proxy(hub, NULL));
	fu_genesys_test_assert_entries_refuse(hub, FWUPD_ERROR_INVALID_DATA);

	/* a replugged hub is a new object and binds the one live HID again */
	{
		g_autoptr(FuDevice) hub_new =
		    fu_genesys_test_hub_new(ctx, "USB\\VID_17EF&PID_1123", 0x1123);

		fu_device_replace(hub_new, hub);
		g_assert_false(fu_device_has_inhibit(hub_new, "hid-proxy-ambiguous"));
		g_assert_true(fu_genesys_usbhub_device_set_proxy(FU_GENESYS_USBHUB_DEVICE(hub_new),
								 hid_b,
								 FU_GENESYS_TEST_HUB_PHYSICAL_ID,
								 &error));
		g_assert_no_error(error);
		g_assert_true(fu_device_get_proxy(hub_new, NULL) == hid_b);
	}
}

static FuDevice *
fu_genesys_test_parent_new(guint16 vid, guint16 pid)
{
	FuDevice *device = g_object_new(FU_TYPE_DEVICE, NULL);
	fu_device_set_vid(device, vid);
	fu_device_set_pid(device, pid);
	return device;
}

/* the value is read back through to_string, the field is private; nocheck:name */
static guint64
fu_genesys_test_parent_pid_of(FuDevice *device)
{
	g_autofree gchar *str = fu_device_to_string(device);
	g_autofree gchar *pid_str = NULL;
	g_autoptr(GMatchInfo) match = NULL;
	g_autoptr(GRegex) re = g_regex_new("ParentHubPid:\\s+0x([0-9a-f]+)", 0, 0, NULL);
	guint64 pid = 0;

	if (!g_regex_match(re, str, 0, &match))
		return 0;
	pid_str = g_match_info_fetch(match, 1);
	g_assert_true(fu_strtoull(pid_str, &pid, 0, G_MAXUINT16, FU_INTEGER_BASE_16, NULL));
	return pid;
}

/* the Hub must sit directly below the expected Genesys Hub, not below a neighbour or a foreign
 * model */
static void
fu_genesys_usbhub_device_p34_parent_hub_func(void)
{
	g_autoptr(FuContext) ctx = fu_genesys_test_context_new();
	g_autoptr(FuDevice) l1 = fu_genesys_test_parent_new(0x17EF, 0x1121);
	g_autoptr(FuDevice) l2 = fu_genesys_test_parent_new(0x17EF, 0x1122);
	g_autoptr(FuDevice) foreign_vid = fu_genesys_test_parent_new(0x05E3, 0x1121);
	g_autoptr(FuDevice) root = fu_genesys_test_parent_new(0x1D6B, 0x0002);
	g_autoptr(GError) error = NULL;

	/* no PID configured: nothing is checked, as for every other Genesys Hub */
	g_assert_true(fu_genesys_usbhub_device_check_parent_hub(0, 0x17EF, NULL, &error));
	g_assert_no_error(error);
	g_assert_true(fu_genesys_usbhub_device_check_parent_hub(0, 0x17EF, root, &error));
	g_assert_no_error(error);

	/* configured: an unresolved parent fails closed */
	g_assert_false(fu_genesys_usbhub_device_check_parent_hub(0x1121, 0x17EF, NULL, &error));
	g_assert_error(error, FWUPD_ERROR, FWUPD_ERROR_NOT_SUPPORTED);
	g_clear_error(&error);

	/* the right parent passes */
	g_assert_true(fu_genesys_usbhub_device_check_parent_hub(0x1121, 0x17EF, l1, &error));
	g_assert_no_error(error);
	g_assert_true(fu_genesys_usbhub_device_check_parent_hub(0x1122, 0x17EF, l2, &error));
	g_assert_no_error(error);

	/* a skipped level, a swapped neighbour, another VID and a root hub are refused */
	g_assert_false(fu_genesys_usbhub_device_check_parent_hub(0x1122, 0x17EF, l1, &error));
	g_assert_error(error, FWUPD_ERROR, FWUPD_ERROR_INVALID_DATA);
	g_clear_error(&error);
	g_assert_false(fu_genesys_usbhub_device_check_parent_hub(0x1121, 0x17EF, l2, &error));
	g_assert_error(error, FWUPD_ERROR, FWUPD_ERROR_INVALID_DATA);
	g_clear_error(&error);
	g_assert_false(
	    fu_genesys_usbhub_device_check_parent_hub(0x1121, 0x17EF, foreign_vid, &error));
	g_assert_error(error, FWUPD_ERROR, FWUPD_ERROR_INVALID_DATA);
	g_clear_error(&error);
	g_assert_false(fu_genesys_usbhub_device_check_parent_hub(0x1121, 0x17EF, root, &error));
	g_assert_error(error, FWUPD_ERROR, FWUPD_ERROR_INVALID_DATA);
	g_clear_error(&error);
}

/* the quirk sets the parent only on 1122 and 1123, never on 1121 or on a hub without a quirk */
static void
fu_genesys_usbhub_device_p34_parent_hub_quirk_func(void)
{
	g_autoptr(FuContext) ctx = fu_genesys_test_context_new();
	struct {
		const gchar *instance_id;
		guint16 pid;
		guint64 expected_parent;
	} hubs[] = {
	    {"USB\\VID_17EF&PID_1121", 0x1121, 0},
	    {"USB\\VID_17EF&PID_1122", 0x1122, 0x1121},
	    {"USB\\VID_17EF&PID_1123", 0x1123, 0x1122},
	    {"USB\\VID_17EF&PID_1159", 0x1159, 0},
	};

	for (guint i = 0; i < G_N_ELEMENTS(hubs); i++) {
		g_autoptr(FuDevice) hub =
		    fu_genesys_test_hub_new(ctx, hubs[i].instance_id, hubs[i].pid);
		g_assert_cmpuint(fu_genesys_test_parent_pid_of(hub), ==, hubs[i].expected_parent);
	}
}

/* the engine path: the hub is built from the donor with fu_device_incorporate(), so the HID model
 * and the parent must survive that, using the GENERIC flags the real USB\VID&PID ID carries */
static void
fu_genesys_usbhub_device_p34_engine_quirk_path_func(void)
{
	g_autoptr(FuContext) ctx = NULL;
	struct {
		const gchar *instance_id;
		guint16 pid;
		guint16 hid_pid;
		guint64 expected_parent;
	} hubs[] = {
	    {"USB\\VID_17EF&PID_1121", 0x1121, 0xF00F, 0},
	    {"USB\\VID_17EF&PID_1122", 0x1122, 0x4C41, 0x1121},
	    {"USB\\VID_17EF&PID_1123", 0x1123, 0x4C42, 0x1122},
	};

	g_type_ensure(FU_TYPE_GENESYS_USBHUB_DEVICE);
	ctx = fu_genesys_test_context_new();

	for (guint i = 0; i < G_N_ELEMENTS(hubs); i++) {
		g_autoptr(FuDevice) donor = g_object_new(FU_TYPE_USB_DEVICE, NULL);
		g_autoptr(FuDevice) hub = NULL;
		g_autoptr(FuDevice) hid = fu_genesys_test_hid_new(0x17EF, hubs[i].hid_pid);
		g_autoptr(GError) error = NULL;
		GType gtype;

		fu_device_set_context(donor, ctx);
		fu_device_add_instance_id_full(donor,
					       hubs[i].instance_id,
					       FU_DEVICE_INSTANCE_FLAG_GENERIC |
						   FU_DEVICE_INSTANCE_FLAG_VISIBLE |
						   FU_DEVICE_INSTANCE_FLAG_QUIRKS);
		fu_device_set_vid(donor, 0x17EF);
		fu_device_set_pid(donor, hubs[i].pid);
		fu_device_set_physical_id(donor, FU_GENESYS_TEST_HUB_PHYSICAL_ID);

		gtype = fu_device_get_specialized_gtype(donor);
		g_assert_cmpuint(gtype, ==, FU_TYPE_GENESYS_USBHUB_DEVICE);
		hub = g_object_new(gtype, "context", ctx, NULL);
		fu_device_incorporate(hub, donor, FU_DEVICE_INCORPORATE_FLAG_ALL);
		fu_genesys_usbhub_device_add_model_quirk_id(FU_GENESYS_USBHUB_DEVICE(hub));

		g_assert_true(
		    fu_device_has_private_flag(hub, FU_GENESYS_USBHUB_FLAG_REQUIRE_HID_PROXY));
		g_assert_cmpuint(fu_genesys_test_parent_pid_of(hub), ==, hubs[i].expected_parent);
		g_assert_true(fu_genesys_usbhub_device_set_proxy(FU_GENESYS_USBHUB_DEVICE(hub),
								 hid,
								 FU_GENESYS_TEST_HUB_PHYSICAL_ID,
								 &error));
		g_assert_no_error(error);
		g_assert_true(fu_device_get_proxy(hub, NULL) == hid);
	}
}

/* a hub with a configured parent refuses every state-changing entry when the parent cannot be
 * resolved */
static void
fu_genesys_usbhub_device_p34_parent_hub_entries_func(void)
{
	g_autoptr(FuContext) ctx = fu_genesys_test_context_new();
	g_autoptr(FuDevice) hub = g_object_new(FU_TYPE_GENESYS_USBHUB_DEVICE, NULL);
	FuDeviceClass *klass = FU_DEVICE_GET_CLASS(hub);
	g_autoptr(FuProgress) progress = fu_progress_new(G_STRLOC);
	g_autoptr(FuFirmware) firmware = fu_firmware_new();
	g_autoptr(GError) error = NULL;

	/* no REQUIRE_HID_PROXY here: the parent gate stands on its own */
	fu_device_set_context(hub, ctx);
	fu_device_set_vid(hub, 0x17EF);
	fu_device_set_pid(hub, 0x1122);
	g_assert_true(fu_device_set_quirk_kv(hub,
					     "GenesysParentHubPid",
					     "0x1121",
					     FU_CONTEXT_QUIRK_SOURCE_DB,
					     &error));
	g_assert_no_error(error);
	g_assert_cmpuint(fu_genesys_test_parent_pid_of(hub), ==, 0x1121);

	g_assert_false(klass->detach(hub, progress, &error));
	g_assert_error(error, FWUPD_ERROR, FWUPD_ERROR_INVALID_DATA);
	g_assert_nonnull(g_strstr_len(error->message, -1, "parent hub"));
	g_clear_error(&error);
	g_assert_false(klass->prepare(hub, progress, FWUPD_INSTALL_FLAG_NONE, &error));
	g_assert_error(error, FWUPD_ERROR, FWUPD_ERROR_INVALID_DATA);
	g_assert_nonnull(g_strstr_len(error->message, -1, "parent hub"));
	g_clear_error(&error);
	g_assert_false(
	    klass->write_firmware(hub, firmware, progress, FWUPD_INSTALL_FLAG_NONE, &error));
	g_assert_error(error, FWUPD_ERROR, FWUPD_ERROR_INVALID_DATA);
	g_assert_nonnull(g_strstr_len(error->message, -1, "parent hub"));
	g_clear_error(&error);
	g_assert_null(klass->dump_firmware(hub, progress, &error));
	g_assert_error(error, FWUPD_ERROR, FWUPD_ERROR_INVALID_DATA);
	g_assert_nonnull(g_strstr_len(error->message, -1, "parent hub"));
}

/* a backend that only answers "who is the parent of this device", as udev/libusb would */
#define FU_TYPE_GENESYS_TEST_BACKEND (fu_genesys_test_backend_get_type())
G_DECLARE_FINAL_TYPE(FuGenesysTestBackend,
		     fu_genesys_test_backend,
		     FU,
		     GENESYS_TEST_BACKEND,
		     FuBackend)

struct _FuGenesysTestBackend {
	FuBackend parent_instance;
	FuDevice *parent; /* NULL: the topology cannot be resolved */
	guint parent_queries;
};

G_DEFINE_TYPE(FuGenesysTestBackend, fu_genesys_test_backend, FU_TYPE_BACKEND)

static FuDevice *
fu_genesys_test_backend_get_device_parent(FuBackend *backend,
					  FuDevice *device,
					  const gchar *subsystem,
					  GError **error)
{
	FuGenesysTestBackend *self = FU_GENESYS_TEST_BACKEND(backend);

	self->parent_queries++;
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

/*
 * Drives the real FuDevice->setup() of a Hub that is not open: the first vendor/USB request of
 * setup() is a transfer on the closed device, which fails with "has not been opened" instead of
 * reaching hardware. So the error tells where setup() stopped, and the wrong-parent case must stop
 * at the parent gate before any transfer (ISP_ENTER and every vendor command come after it).
 */
static gboolean
fu_genesys_test_setup_hub(guint16 hub_pid,
			  guint16 expected_parent_pid,
			  FuDevice *parent,
			  guint *parent_queries,
			  GError **error)
{
	g_autoptr(FuContext) ctx = fu_genesys_test_context_new();
	g_autoptr(FuGenesysTestBackend) backend = g_object_new(FU_TYPE_GENESYS_TEST_BACKEND,
							       "name",
							       "genesys-test",
							       "context",
							       ctx,
							       NULL);
	g_autoptr(FuDevice) hub = g_object_new(FU_TYPE_GENESYS_USBHUB_DEVICE, NULL);
	g_autofree gchar *pid_str = g_strdup_printf("0x%04x", expected_parent_pid);
	gboolean ret;

	backend->parent = parent;
	fu_device_set_context(hub, ctx);
	fu_device_set_vid(hub, 0x17EF);
	fu_device_set_pid(hub, hub_pid);
	fu_device_set_backend(hub, FU_BACKEND(backend));
	if (!fu_device_set_quirk_kv(hub,
				    "GenesysParentHubPid",
				    pid_str,
				    FU_CONTEXT_QUIRK_SOURCE_DB,
				    error))
		return FALSE;
	ret = FU_DEVICE_GET_CLASS(hub)->setup(hub, error);
	*parent_queries = backend->parent_queries;
	return ret;
}

/* a Hub below the wrong, a missing or an unresolvable parent is refused before any transfer */
static void
fu_genesys_usbhub_device_p34_setup_order_func(void)
{
	g_autoptr(FuDevice) right = fu_genesys_test_parent_new(0x17EF, 0x1122);
	g_autoptr(FuDevice) skipped_level = fu_genesys_test_parent_new(0x17EF, 0x1121);
	g_autoptr(FuDevice) foreign_vid = fu_genesys_test_parent_new(0x05E3, 0x1122);
	g_autoptr(FuDevice) root = fu_genesys_test_parent_new(0x1D6B, 0x0002);
	struct {
		const gchar *name;
		FuDevice *parent;
	} wrong[] = {
	    {"skipped level", skipped_level},
	    {"foreign vid", foreign_vid},
	    {"root hub", root},
	    {"unresolvable", NULL},
	};
	g_autoptr(GError) error = NULL;
	guint queries = 0;

	/* control: the right parent passes the gate and setup() goes on to the closed device */
	g_assert_false(fu_genesys_test_setup_hub(0x1123, 0x1122, right, &queries, &error));
	g_assert_cmpuint(queries, ==, 1);
	g_assert_nonnull(error);
	g_test_message("control error: %s", error->message);
	g_assert_null(g_strstr_len(error->message, -1, "parent hub"));
	g_assert_nonnull(g_strstr_len(error->message, -1, "has not been opened"));
	g_clear_error(&error);

	for (guint i = 0; i < G_N_ELEMENTS(wrong); i++) {
		g_test_message("wrong parent: %s", wrong[i].name);
		g_assert_false(
		    fu_genesys_test_setup_hub(0x1123, 0x1122, wrong[i].parent, &queries, &error));
		g_assert_cmpuint(queries, ==, 1);
		g_assert_nonnull(error);
		g_assert_error(error, FWUPD_ERROR, FWUPD_ERROR_INVALID_DATA);
		g_assert_nonnull(g_strstr_len(error->message, -1, "parent hub"));
		g_assert_null(g_strstr_len(error->message, -1, "has not been opened"));
		g_clear_error(&error);
	}
}

/* no install flag lets an older image past the bank refusal, which comes before ISP_ENTER */
static void
fu_genesys_usbhub_device_p34_update_bank_flags_func(void)
{
	const FwupdInstallFlags flags[] = {
	    FWUPD_INSTALL_FLAG_NONE,
	    FWUPD_INSTALL_FLAG_FORCE,
	    FWUPD_INSTALL_FLAG_ALLOW_OLDER,
	    FWUPD_INSTALL_FLAG_ALLOW_REINSTALL,
	    FWUPD_INSTALL_FLAG_IGNORE_VID_PID,
	    FWUPD_INSTALL_FLAG_FORCE | FWUPD_INSTALL_FLAG_ALLOW_OLDER |
		FWUPD_INSTALL_FLAG_ALLOW_REINSTALL | FWUPD_INSTALL_FLAG_IGNORE_VID_PID |
		FWUPD_INSTALL_FLAG_IGNORE_CHECKSUM,
	};
	g_autoptr(FuContext) ctx = fu_genesys_test_context_new();
	g_autoptr(FuDevice) hub = fu_genesys_test_hub_new(ctx, "USB\\VID_17EF&PID_1121", 0x1121);
	g_autoptr(FuDevice) hid = fu_genesys_test_hid_new(0x17EF, 0xF00F);
	g_autoptr(FuFirmware) firmware = fu_firmware_new();
	g_autoptr(FuProgress) progress = fu_progress_new(G_STRLOC);
	g_autoptr(FuDevice) hid_parent = fu_genesys_test_parent_new(0x17EF, 0x1121);
	g_autoptr(FuGenesysTestBackend) backend = g_object_new(FU_TYPE_GENESYS_TEST_BACKEND,
							       "name",
							       "genesys-test",
							       "context",
							       ctx,
							       NULL);
	g_autoptr(GError) error = NULL;

	/* the HID sits below the hub, so the proxy gate passes and write_firmware() goes on */
	fu_device_set_physical_id(hid_parent, FU_GENESYS_TEST_HUB_PHYSICAL_ID);
	backend->parent = hid_parent;
	fu_device_set_backend(hid, FU_BACKEND(backend));
	g_assert_true(fu_genesys_usbhub_device_set_proxy(FU_GENESYS_USBHUB_DEVICE(hub),
							 hid,
							 FU_GENESYS_TEST_HUB_PHYSICAL_ID,
							 &error));
	g_assert_no_error(error);

	/* bank1 86.24 is written while bank2 holds 86.30, so 86.17 would never run */
	fu_genesys_usbhub_device_set_gl3525_bank_state(FU_GENESYS_USBHUB_DEVICE(hub),
						       0x8624,
						       0x8630,
						       FALSE,
						       TRUE);
	fu_firmware_set_version_raw(firmware, 0x8617);
	for (guint i = 0; i < G_N_ELEMENTS(flags); i++) {
		g_autoptr(GError) error_write = NULL;

		g_assert_false(FU_DEVICE_GET_CLASS(hub)->write_firmware(hub,
									firmware,
									progress,
									flags[i],
									&error_write));
		g_assert_error(error_write, FWUPD_ERROR, FWUPD_ERROR_NOT_SUPPORTED);
		g_assert_nonnull(g_strstr_len(error_write->message, -1, "newest bank"));
	}

	/* bank versions not read: even an upgrade is refused, before ISP_ENTER */
	fu_genesys_usbhub_device_set_gl3525_bank_state(FU_GENESYS_USBHUB_DEVICE(hub),
						       0x0000,
						       0x0000,
						       FALSE,
						       FALSE);
	fu_firmware_set_version_raw(firmware, 0x8630);
	for (guint i = 0; i < G_N_ELEMENTS(flags); i++) {
		g_autoptr(GError) error_write = NULL;

		g_assert_false(FU_DEVICE_GET_CLASS(hub)->write_firmware(hub,
									firmware,
									progress,
									flags[i],
									&error_write));
		g_assert_error(error_write, FWUPD_ERROR, FWUPD_ERROR_NOT_SUPPORTED);
		g_assert_nonnull(g_strstr_len(error_write->message, -1, "were not read"));
	}
}

/* the context of fu_genesys_test_context_new(), but with the quirk files of another directory */
static FuContext *
fu_genesys_test_context_new_from_dir(const gchar *quirks_dir)
{
	g_autoptr(FuContext) ctx = g_object_new(FU_TYPE_CONTEXT, NULL);
	g_autoptr(FuProgress) progress = fu_progress_new(G_STRLOC);
	g_autoptr(GError) error = NULL;

	fu_context_set_path(ctx, FU_PATH_KIND_DATADIR_QUIRKS, quirks_dir);
	fu_context_add_flag(ctx, FU_CONTEXT_FLAG_NO_CACHE);
	g_assert_true(fu_context_load(ctx, progress, FU_CONTEXT_LOAD_FLAG_NONE, &error));
	g_assert_no_error(error);
	return g_steal_pointer(&ctx);
}

/* what cfi_setup() does for one flash ID read from the hub: the chip is only known to the
 * plugin if data/cfi.quirk is in the quirk path, otherwise setup() ends with "no CFI device found"
 */
static FuCfiDevice *
fu_genesys_test_cfi_lookup(FuContext *ctx, FuDevice *proxy, const gchar *flash_id)
{
	g_autoptr(FuCfiDevice) cfi = fu_cfi_device_new(proxy, flash_id);
	g_autoptr(GError) error = NULL;

	g_assert_true(fu_device_setup(FU_DEVICE(cfi), &error));
	g_assert_no_error(error);
	return g_steal_pointer(&cfi);
}

/* C84012 is the GD25Q20C of the P34WD-40 hub; the repo's data/cfi.quirk must resolve it */
static void
fu_genesys_usbhub_device_p34_cfi_c84012_func(void)
{
	g_autofree gchar *repo_quirk =
	    g_build_filename(SRCDIR, "..", "..", "data", "cfi.quirk", NULL);
	g_autofree gchar *tmpdir = g_dir_make_tmp("fwupd-genesys-cfi-XXXXXX", NULL);
	g_autofree gchar *copy = NULL;
	g_autofree gchar *contents = NULL;
	gsize contentsz = 0;
	g_autoptr(FuContext) ctx_plugin_only = fu_genesys_test_context_new();
	g_autoptr(FuContext) ctx_with_cfi = NULL;
	g_autoptr(FuDevice) proxy_a = fu_device_new(ctx_plugin_only);
	g_autoptr(FuDevice) proxy_b = NULL;
	g_autoptr(FuCfiDevice) cfi = NULL;
	g_autoptr(GError) error = NULL;

	g_assert_nonnull(tmpdir);
	g_assert_true(g_file_get_contents(repo_quirk, &contents, &contentsz, &error));
	g_assert_no_error(error);
	copy = g_build_filename(tmpdir, "cfi.quirk", NULL);
	g_assert_true(g_file_set_contents(copy, contents, contentsz, &error));
	g_assert_no_error(error);

	/* with the quirks of the plugin only the chip is not recognised */
	cfi = fu_genesys_test_cfi_lookup(ctx_plugin_only, proxy_a, "C84012");
	g_assert_null(fu_device_get_name(FU_DEVICE(cfi)));
	g_assert_cmpuint(fu_cfi_device_get_size(cfi), ==, 0);
	g_clear_object(&cfi);

	/* with the repo's cfi.quirk the chip is the GD25Q20C of 256 KiB */
	ctx_with_cfi = fu_genesys_test_context_new_from_dir(tmpdir);
	proxy_b = fu_device_new(ctx_with_cfi);
	cfi = fu_genesys_test_cfi_lookup(ctx_with_cfi, proxy_b, "C84012");
	g_assert_cmpstr(fu_device_get_name(FU_DEVICE(cfi)), ==, "GD25Q20C");
	g_assert_cmpuint(fu_cfi_device_get_size(cfi), ==, 0x40000);
	g_clear_object(&cfi);

	/* a shorter read only gets the generic family entry, without a size: the 3 byte read
	 * decides */
	cfi = fu_genesys_test_cfi_lookup(ctx_with_cfi, proxy_b, "C840");
	g_assert_cmpstr(fu_device_get_name(FU_DEVICE(cfi)), ==, "GD25Qxxx");
	g_assert_cmpuint(fu_cfi_device_get_size(cfi), ==, 0);
	g_clear_object(&cfi);

	/* the five reads on a 1123 hub, in the order of cfi_setup(): the first
	 * recognised chip wins, so none of the earlier reads may claim the hub */
	{
		const gchar *reads[] = {"FFFF", "FF34", "C811", "C84012", "1111"};
		const gchar *first_name = NULL;
		guint first_idx = G_MAXUINT;

		for (guint i = 0; i < G_N_ELEMENTS(reads); i++) {
			g_autoptr(FuCfiDevice) probe =
			    fu_genesys_test_cfi_lookup(ctx_with_cfi, proxy_b, reads[i]);
			if (fu_device_get_name(FU_DEVICE(probe)) != NULL) {
				first_name = g_intern_string(fu_device_get_name(FU_DEVICE(probe)));
				first_idx = i;
				break;
			}
		}
		g_assert_cmpuint(first_idx, ==, 3);
		g_assert_cmpstr(first_name, ==, "GD25Q20C");
	}

	/* a chip that is not listed stays unrecognised, even with cfi.quirk loaded */
	cfi = fu_genesys_test_cfi_lookup(ctx_with_cfi, proxy_b, "FFFFFF");
	g_assert_null(fu_device_get_name(FU_DEVICE(cfi)));

	(void)g_remove(copy);
	(void)g_rmdir(tmpdir);
}

/* the chunks of one transfer tile the range exactly and never leave their page or packet size */
static void
fu_genesys_test_chunks_check(gsize size, gsize addr, gsize page, gsize packet)
{
	gsize next = addr;
	gsize total = 0;
	g_autoptr(GPtrArray) chunks = NULL;
	g_autoptr(GError) error = NULL;

	chunks = fu_genesys_usbhub_device_chunks_new(NULL, size, addr, page, packet, FALSE, &error);
	g_assert_no_error(error);
	g_assert_nonnull(chunks);
	for (guint i = 0; i < chunks->len; i++) {
		FuChunk *chk = g_ptr_array_index(chunks, i);
		gsize abs = fu_chunk_get_address(chk);
		gsize sz = fu_chunk_get_data_sz(chk);

		if (page != 0) {
			abs += fu_chunk_get_page(chk) * page;
			g_assert_cmpuint(fu_chunk_get_address(chk) + sz, <=, page);
		}
		g_assert_cmpuint(fu_chunk_get_idx(chk), ==, i);
		g_assert_cmpuint(sz, >, 0);
		g_assert_cmpuint(sz, <=, packet);
		g_assert_cmpuint(abs, ==, next);
		next += sz;
		total += sz;
	}
	g_assert_cmpuint(total, ==, size);
}

/* a flash transfer must never leave its 64 KB window: the P34 HID dump once sent 0xffc0 at 0xffc0
 */
static void
fu_genesys_usbhub_device_chunks_page_boundary_func(void)
{
	const gsize pages[] = {0x10000, 0};
	const gsize packets[] = {0x40, 0x200, 0x1000, 0xffc0};
	const gsize addrs[] = {0x0, 0xb000, 0xffc0, 0x1234, 0x10000};
	const gsize sizes[] = {0x1, 0x400, 0x7c00, 0xb000, 0x40000};
	g_autoptr(GPtrArray) dump = NULL;
	g_autoptr(GPtrArray) bank2 = NULL;
	g_autofree guint8 *buf = g_malloc0(0x40000);
	g_autoptr(GPtrArray) mut = NULL;
	g_autoptr(GError) error = NULL;

	/* the five P34 dump reads of Dump #4, now cut at each 64 KB */
	dump =
	    fu_genesys_usbhub_device_chunks_new(NULL, 0x40000, 0, 0x10000, 0xffc0, FALSE, &error);
	g_assert_no_error(error);
	g_assert_nonnull(dump);
	g_assert_cmpuint(dump->len, ==, 8);
	for (guint i = 0; i < dump->len; i++) {
		FuChunk *chk = g_ptr_array_index(dump, i);
		g_assert_cmpuint(fu_chunk_get_idx(chk), ==, i);
		g_assert_cmpuint(fu_chunk_get_page(chk), ==, i / 2);
		g_assert_cmpuint(fu_chunk_get_address(chk), ==, (i % 2) == 0 ? 0 : 0xffc0);
		g_assert_cmpuint(fu_chunk_get_data_sz(chk), ==, (i % 2) == 0 ? 0xffc0 : 0x40);
	}

	/* 1121 hub bank2 crosses 0x10000 */
	bank2 = fu_genesys_usbhub_device_chunks_new(NULL,
						    0xb000,
						    0xb000,
						    0x10000,
						    0xffc0,
						    FALSE,
						    &error);
	g_assert_no_error(error);
	g_assert_nonnull(bank2);
	g_assert_cmpuint(bank2->len, ==, 2);
	g_assert_cmpuint(fu_chunk_get_page(g_ptr_array_index(bank2, 0)), ==, 0);
	g_assert_cmpuint(fu_chunk_get_address(g_ptr_array_index(bank2, 0)), ==, 0xb000);
	g_assert_cmpuint(fu_chunk_get_data_sz(g_ptr_array_index(bank2, 0)), ==, 0x5000);
	g_assert_cmpuint(fu_chunk_get_page(g_ptr_array_index(bank2, 1)), ==, 1);
	g_assert_cmpuint(fu_chunk_get_address(g_ptr_array_index(bank2, 1)), ==, 0);
	g_assert_cmpuint(fu_chunk_get_data_sz(g_ptr_array_index(bank2, 1)), ==, 0x6000);

	/* invariants for every combination */
	for (guint p = 0; p < G_N_ELEMENTS(pages); p++) {
		for (guint k = 0; k < G_N_ELEMENTS(packets); k++) {
			for (guint a = 0; a < G_N_ELEMENTS(addrs); a++) {
				for (guint s = 0; s < G_N_ELEMENTS(sizes); s++) {
					if (pages[p] != 0 && pages[p] < packets[k])
						continue;
					fu_genesys_test_chunks_check(sizes[s],
								     addrs[a],
								     pages[p],
								     packets[k]);
				}
			}
		}
	}

	/* the mutable variant keeps pointing into the caller buffer */
	mut = fu_genesys_usbhub_device_chunks_new(buf,
						  0x20000,
						  0xffc0,
						  0x10000,
						  0xffc0,
						  TRUE,
						  &error);
	g_assert_no_error(error);
	g_assert_nonnull(mut);
	for (guint i = 0, off = 0; i < mut->len; i++) {
		FuChunk *chk = g_ptr_array_index(mut, i);
		g_assert_true(fu_chunk_get_data_out(chk) == buf + off);
		off += fu_chunk_get_data_sz(chk);
	}
}

static void
fu_genesys_usbhub_device_p34_bank_runs_target_func(void)
{
	g_autoptr(GError) error = NULL;

	/* both banks 86.24: same, newer or blank other bank is fine */
	g_assert_true(fu_genesys_usbhub_device_check_bank_runs_target(0x8624, 0x8624, &error));
	g_assert_no_error(error);
	g_assert_true(fu_genesys_usbhub_device_check_bank_runs_target(0x8630, 0x8624, &error));
	g_assert_no_error(error);
	g_assert_true(fu_genesys_usbhub_device_check_bank_runs_target(0x8617, 0x0000, &error));
	g_assert_no_error(error);

	/* measured on a 1121: 86.17 beside 86.24 in the other bank never ran */
	g_assert_false(fu_genesys_usbhub_device_check_bank_runs_target(0x8617, 0x8624, &error));
	g_assert_error(error, FWUPD_ERROR, FWUPD_ERROR_NOT_SUPPORTED);
	g_assert_nonnull(g_strstr_len(error->message, -1, "86.17"));
	g_assert_nonnull(g_strstr_len(error->message, -1, "86.24"));
	g_clear_error(&error);

	/* one step older is enough */
	g_assert_false(fu_genesys_usbhub_device_check_bank_runs_target(0x8623, 0x8624, &error));
	g_assert_error(error, FWUPD_ERROR, FWUPD_ERROR_NOT_SUPPORTED);
}

static void
fu_genesys_test_update_bank_expect(gboolean newest_bank_runs,
				   gboolean banks_known,
				   gboolean update_bank2,
				   guint16 bank1,
				   guint16 bank2,
				   guint16 target,
				   gboolean expect_ok)
{
	g_autoptr(GError) error = NULL;
	gboolean ret = fu_genesys_usbhub_device_check_update_bank(newest_bank_runs,
								  banks_known,
								  update_bank2,
								  bank1,
								  bank2,
								  target,
								  &error);
	g_assert_cmpint(ret, ==, expect_ok);
	if (expect_ok)
		g_assert_no_error(error);
	else
		g_assert_error(error, FWUPD_ERROR, FWUPD_ERROR_NOT_SUPPORTED);
}

static void
fu_genesys_usbhub_device_p34_update_bank_func(void)
{
	/* 1121 banks 86.24/86.24: prepare picks bank1 */
	fu_genesys_test_update_bank_expect(TRUE, TRUE, FALSE, 0x8624, 0x8624, 0x8617, FALSE);
	fu_genesys_test_update_bank_expect(TRUE, TRUE, FALSE, 0x8624, 0x8624, 0x8623, FALSE);
	fu_genesys_test_update_bank_expect(TRUE, TRUE, FALSE, 0x8624, 0x8624, 0x8624, TRUE);
	fu_genesys_test_update_bank_expect(TRUE, TRUE, FALSE, 0x8624, 0x8624, 0x8630, TRUE);

	/* bank1 newer than bank2: prepare picks bank2, so bank1 is the one that stays */
	fu_genesys_test_update_bank_expect(TRUE, TRUE, TRUE, 0x8630, 0x8624, 0x8624, FALSE);
	fu_genesys_test_update_bank_expect(TRUE, TRUE, TRUE, 0x8630, 0x8624, 0x8630, TRUE);
	fu_genesys_test_update_bank_expect(TRUE, TRUE, TRUE, 0x8630, 0x8624, 0x8640, TRUE);

	/* 86.17 beside 86.24: the older image is refused, the 86.24 restore is allowed */
	fu_genesys_test_update_bank_expect(TRUE, TRUE, FALSE, 0x8617, 0x8624, 0x8617, FALSE);
	fu_genesys_test_update_bank_expect(TRUE, TRUE, FALSE, 0x8617, 0x8624, 0x8624, TRUE);

	/* an invalid other bank reads as 0: nothing to lose, but the kept bank still counts */
	fu_genesys_test_update_bank_expect(TRUE, TRUE, FALSE, 0x8624, 0x0000, 0x8617, TRUE);
	fu_genesys_test_update_bank_expect(TRUE, TRUE, TRUE, 0x8624, 0x0000, 0x8617, FALSE);

	/* versions not read: cannot tell, so refuse even an upgrade */
	fu_genesys_test_update_bank_expect(TRUE, FALSE, FALSE, 0x0000, 0x0000, 0x8630, FALSE);

	/* not GL3525 dual-image (1122/1123, both banks invalid): never refused */
	fu_genesys_test_update_bank_expect(FALSE, TRUE, FALSE, 0x8716, 0x8716, 0x8710, TRUE);
	fu_genesys_test_update_bank_expect(FALSE, TRUE, FALSE, 0x8710, 0x8716, 0x8710, TRUE);
	fu_genesys_test_update_bank_expect(FALSE, FALSE, FALSE, 0x0000, 0x0000, 0x8710, TRUE);
}

int
main(int argc, char **argv)
{
	g_setenv("G_TEST_SRCDIR", SRCDIR, FALSE);
	g_test_init(&argc, &argv, NULL);
	g_test_add_func("/fwupd/genesys/project-compatibility/matrix",
			fu_genesys_usbhub_device_project_compatibility_matrix_func);
	g_test_add_func("/fwupd/genesys/project-compatibility/ic-mismatch",
			fu_genesys_usbhub_device_project_compatibility_ic_mismatch_func);
	g_test_add_func("/fwupd/genesys/project-compatibility/missing-data",
			fu_genesys_usbhub_device_project_compatibility_missing_data_func);
	g_test_add_func("/fwupd/genesys/project-compatibility/non-strict",
			fu_genesys_usbhub_device_project_compatibility_non_strict_func);
	g_test_add_func("/fwupd/genesys/quirk/p34-hid",
			fu_genesys_usbhub_device_p34_hid_quirk_func);
	g_test_add_func("/fwupd/genesys/hid-proxy/missing",
			fu_genesys_usbhub_device_hid_proxy_missing_func);
	g_test_add_func("/fwupd/genesys/hid-proxy/correct",
			fu_genesys_usbhub_device_hid_proxy_correct_func);
	g_test_add_func("/fwupd/genesys/hid-proxy/wrong-hub",
			fu_genesys_usbhub_device_hid_proxy_wrong_hub_func);
	g_test_add_func("/fwupd/genesys/hid-proxy/replug",
			fu_genesys_usbhub_device_hid_proxy_replug_func);
	g_test_add_func("/fwupd/genesys/hid-proxy/no-regression",
			fu_genesys_usbhub_device_hid_proxy_no_regression_func);
	g_test_add_func("/fwupd/genesys/identity/cross-model",
			fu_genesys_usbhub_device_identity_cross_model_func);
	g_test_add_func("/fwupd/genesys/identity/unknown-identity",
			fu_genesys_usbhub_device_identity_unknown_identity_func);
	g_test_add_func("/fwupd/genesys/identity/check-firmware-not-relaxed",
			fu_genesys_usbhub_device_identity_check_firmware_not_relaxed_func);
	g_test_add_func("/fwupd/genesys/identity/shared-hid-own-dock",
			fu_genesys_usbhub_device_identity_shared_hid_own_dock_func);
	g_test_add_func("/fwupd/genesys/identity/shared-hid-refused",
			fu_genesys_usbhub_device_identity_shared_hid_refused_func);
	g_test_add_func("/fwupd/genesys/p34/quirk", fu_genesys_usbhub_device_p34_quirk_func);
	g_test_add_func("/fwupd/genesys/p34/blank-identity",
			fu_genesys_usbhub_device_p34_blank_identity_func);
	g_test_add_func("/fwupd/genesys/p34/hid-candidates",
			fu_genesys_usbhub_device_p34_hid_candidates_func);
	g_test_add_func("/fwupd/genesys/p34/single-hid-replug",
			fu_genesys_usbhub_device_p34_single_hid_replug_func);
	g_test_add_func("/fwupd/genesys/p34/single-hid-ambiguous",
			fu_genesys_usbhub_device_p34_single_hid_ambiguous_func);
	g_test_add_func("/fwupd/genesys/p34/parent-hub",
			fu_genesys_usbhub_device_p34_parent_hub_func);
	g_test_add_func("/fwupd/genesys/p34/parent-hub-quirk",
			fu_genesys_usbhub_device_p34_parent_hub_quirk_func);
	g_test_add_func("/fwupd/genesys/p34/parent-hub-entries",
			fu_genesys_usbhub_device_p34_parent_hub_entries_func);
	g_test_add_func("/fwupd/genesys/p34/setup-order",
			fu_genesys_usbhub_device_p34_setup_order_func);
	g_test_add_func("/fwupd/genesys/p34/cfi-c84012",
			fu_genesys_usbhub_device_p34_cfi_c84012_func);
	g_test_add_func("/fwupd/genesys/p34/engine-quirk-path",
			fu_genesys_usbhub_device_p34_engine_quirk_path_func);
	g_test_add_func("/fwupd/genesys/p34/chunks-page-boundary",
			fu_genesys_usbhub_device_chunks_page_boundary_func);
	g_test_add_func("/fwupd/genesys/p34/bank-runs-target",
			fu_genesys_usbhub_device_p34_bank_runs_target_func);
	g_test_add_func("/fwupd/genesys/p34/update-bank",
			fu_genesys_usbhub_device_p34_update_bank_func);
	g_test_add_func("/fwupd/genesys/p34/update-bank-flags",
			fu_genesys_usbhub_device_p34_update_bank_flags_func);
	return g_test_run();
}