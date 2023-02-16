/*
 * Copyright (C) 2015 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include <fwupdplugin.h>

#include <glib/gstdio.h>
#include <stdlib.h>

#include "../uefi-capsule/fu-uefi-capsule-plugin.h"
#include "fu-context-private.h"
#include "fu-dell-plugin.h"
#include "fu-device-private.h"
#include "fu-plugin-dell.h"
#include "fu-plugin-private.h"

typedef struct {
	FuPlugin *plugin_uefi_capsule;
	FuPlugin *plugin_dell;
} FuTest;

static FuDevice *
_find_device_by_id(GPtrArray *devices, const gchar *device_id)
{
	for (guint i = 0; i < devices->len; i++) {
		FuDevice *device = g_ptr_array_index(devices, i);
		if (g_strcmp0(fu_device_get_id(device), device_id) == 0)
			return device;
	}
	return NULL;
}

static FuDevice *
_find_device_by_name(GPtrArray *devices, const gchar *device_id)
{
	for (guint i = 0; i < devices->len; i++) {
		FuDevice *device = g_ptr_array_index(devices, i);
		if (g_strcmp0(fu_device_get_name(device), device_id) == 0)
			return device;
	}
	return NULL;
}

static void
_plugin_device_added_cb(FuPlugin *plugin, FuDevice *device, gpointer user_data)
{
	GPtrArray *devices = (GPtrArray *)user_data;
	if (fu_device_get_alternate_id(device) != NULL) {
		FuDevice *device_alt =
		    _find_device_by_id(devices, fu_device_get_alternate_id(device));
		if (device_alt != NULL)
			fu_device_set_alternate(device, device_alt);
	}
	g_ptr_array_add(devices, g_object_ref(device));
}

static void
fu_engine_plugin_device_register_cb(FuPlugin *plugin_dell, FuDevice *device, gpointer user_data)
{
	FuPlugin *plugin_uefi_capsule = FU_PLUGIN(user_data);
	g_autofree gchar *dbg = fu_device_to_string(device);
	g_debug("registering device: %s", dbg);
	fu_plugin_runner_device_register(plugin_uefi_capsule, device);
}

static void
fu_dell_plugin_tpm_func(gconstpointer user_data)
{
	FuTest *self = (FuTest *)user_data;
	FuDevice *device_v12;
	FuDevice *device_v20;
	const guint8 fw[30] = {'F', 'W', 0x00};
	gboolean ret;
	gulong added_id;
	gulong register_id;
	struct tpm_status tpm_out;
	const gchar *tpm_server_running = g_getenv("TPM_SERVER_RUNNING");
	g_autoptr(GBytes) blob_fw = g_bytes_new_static(fw, sizeof(fw));
	g_autoptr(GError) error = NULL;
	g_autoptr(GPtrArray) devices = NULL;
	g_autoptr(FuProgress) progress = fu_progress_new(G_STRLOC);

#ifdef HAVE_GETUID
	if (tpm_server_running == NULL && (getuid() != 0 || geteuid() != 0)) {
		g_test_skip("TPM tests require simulated TPM2.0 running or need root access with "
			    "physical TPM");
		return;
	}
#endif

	memset(&tpm_out, 0x0, sizeof(tpm_out));

	devices = g_ptr_array_new_with_free_func((GDestroyNotify)g_object_unref);
	added_id = g_signal_connect(FU_PLUGIN(self->plugin_uefi_capsule),
				    "device-added",
				    G_CALLBACK(_plugin_device_added_cb),
				    devices);

	register_id = g_signal_connect(FU_PLUGIN(self->plugin_dell),
				       "device-register",
				       G_CALLBACK(fu_engine_plugin_device_register_cb),
				       self->plugin_uefi_capsule);
	ret = fu_plugin_runner_coldplug(self->plugin_dell, progress, &error);
	g_assert_no_error(error);
	g_assert_true(ret);

	/* inject fake data (no TPM) */
	tpm_out.ret = -2;
	fu_dell_plugin_inject_fake_data(self->plugin_dell, (guint32 *)&tpm_out, 0, 0, NULL, FALSE);
	ret = fu_dell_plugin_detect_tpm(self->plugin_dell, &error);
	g_assert_error(error, G_IO_ERROR, G_IO_ERROR_NOT_SUPPORTED);
	g_assert_false(ret);
	g_assert_cmpint(devices->len, ==, 0);
	g_clear_error(&error);

	/* inject fake data:
	 * - that is out of flashes
	 * - no ownership
	 * - TPM 1.2
	 * dev will be the locked 2.0, alt will be the orig 1.2
	 */
	tpm_out.ret = 0;
	tpm_out.fw_version = 0;
	tpm_out.status = TPM_EN_MASK | (TPM_1_2_MODE << 8);
	tpm_out.flashes_left = 0;
	fu_dell_plugin_inject_fake_data(self->plugin_dell, (guint32 *)&tpm_out, 0, 0, NULL, TRUE);
	ret = fu_dell_plugin_detect_tpm(self->plugin_dell, &error);
	g_assert_true(ret);
	g_assert_cmpint(devices->len, ==, 2);

	/* make sure 2.0 is locked */
	device_v20 = _find_device_by_name(devices, "TPM 2.0");
	g_assert_nonnull(device_v20);
	g_assert_true(fu_device_has_flag(device_v20, FWUPD_DEVICE_FLAG_LOCKED));

	/* make sure not allowed to flash 1.2 */
	device_v12 = _find_device_by_name(devices, "TPM 1.2");
	g_assert_nonnull(device_v12);
	g_assert_false(fu_device_has_flag(device_v12, FWUPD_DEVICE_FLAG_UPDATABLE));

	/* try to unlock 2.0 */
	ret = fu_plugin_runner_unlock(self->plugin_uefi_capsule, device_v20, &error);
	g_assert_error(error, FWUPD_ERROR, FWUPD_ERROR_NOT_SUPPORTED);
	g_assert_false(ret);
	g_clear_error(&error);

	/* cleanup */
	g_ptr_array_set_size(devices, 0);

	/* inject fake data:
	 * - that has flashes
	 * - owned
	 * - TPM 1.2
	 * dev will be the locked 2.0, alt will be the orig 1.2
	 */
	tpm_out.status = TPM_EN_MASK | TPM_OWN_MASK | (TPM_1_2_MODE << 8);
	tpm_out.flashes_left = 125;
	fu_dell_plugin_inject_fake_data(self->plugin_dell, (guint32 *)&tpm_out, 0, 0, NULL, TRUE);
	ret = fu_dell_plugin_detect_tpm(self->plugin_dell, &error);
	g_assert_no_error(error);
	g_assert_true(ret);

	/* make sure not allowed to flash 1.2 */
	device_v12 = _find_device_by_name(devices, "TPM 1.2");
	g_assert_nonnull(device_v12);
	g_assert_false(fu_device_has_flag(device_v12, FWUPD_DEVICE_FLAG_UPDATABLE));

	/* try to unlock 2.0 */
	device_v20 = _find_device_by_name(devices, "TPM 2.0");
	g_assert_nonnull(device_v20);
	ret = fu_plugin_runner_unlock(self->plugin_uefi_capsule, device_v20, &error);
	g_assert_error(error, FWUPD_ERROR, FWUPD_ERROR_NOT_SUPPORTED);
	g_assert_false(ret);
	g_clear_error(&error);

	/* cleanup */
	g_ptr_array_set_size(devices, 0);

	/* inject fake data:
	 * - that has flashes
	 * - not owned
	 * - TPM 1.2
	 * dev will be the locked 2.0, alt will be the orig 1.2
	 */
	tpm_out.status = TPM_EN_MASK | (TPM_1_2_MODE << 8);
	tpm_out.flashes_left = 125;
	fu_dell_plugin_inject_fake_data(self->plugin_dell, (guint32 *)&tpm_out, 0, 0, NULL, TRUE);
	ret = fu_dell_plugin_detect_tpm(self->plugin_dell, &error);
	g_assert_no_error(error);
	g_assert_true(ret);

	/* make sure allowed to flash 1.2 but not 2.0 */
	device_v12 = _find_device_by_name(devices, "TPM 1.2");
	g_assert_nonnull(device_v12);
	g_assert_true(fu_device_has_flag(device_v12, FWUPD_DEVICE_FLAG_UPDATABLE));
	device_v20 = _find_device_by_name(devices, "TPM 2.0");
	g_assert_nonnull(device_v20);
	g_assert_false(fu_device_has_flag(device_v20, FWUPD_DEVICE_FLAG_UPDATABLE));

	/* try to unlock 2.0 */
	ret = fu_plugin_runner_unlock(self->plugin_uefi_capsule, device_v20, &error);
	g_assert_no_error(error);
	g_assert_true(ret);

	/* make sure no longer allowed to flash 1.2 but can flash 2.0 */
	g_assert_false(fu_device_has_flag(device_v12, FWUPD_DEVICE_FLAG_UPDATABLE));
	g_assert_true(fu_device_has_flag(device_v20, FWUPD_DEVICE_FLAG_UPDATABLE));

	/* cleanup */
	g_ptr_array_set_size(devices, 0);

	/* inject fake data:
	 * - that has 1 flash left
	 * - not owned
	 * - TPM 2.0
	 * dev will be the locked 1.2, alt will be the orig 2.0
	 */
	tpm_out.status = TPM_EN_MASK | (TPM_2_0_MODE << 8);
	tpm_out.flashes_left = 1;
	fu_dell_plugin_inject_fake_data(self->plugin_dell, (guint32 *)&tpm_out, 0, 0, NULL, TRUE);
	ret = fu_dell_plugin_detect_tpm(self->plugin_dell, &error);
	g_assert_no_error(error);
	g_assert_true(ret);

	/* make sure allowed to flash 2.0 but not 1.2 */
	device_v20 = _find_device_by_name(devices, "TPM 2.0");
	g_assert_nonnull(device_v20);
	g_assert_true(fu_device_has_flag(device_v20, FWUPD_DEVICE_FLAG_UPDATABLE));
	device_v12 = _find_device_by_name(devices, "TPM 1.2");
	g_assert_nonnull(device_v12);
	g_assert_false(fu_device_has_flag(device_v12, FWUPD_DEVICE_FLAG_UPDATABLE));

	/* ensure flags set */
	ret = fu_device_probe(device_v20, &error);
	g_assert_no_error(error);
	g_assert_true(ret);

	/* With one flash left we need an override */
	ret = fu_plugin_runner_write_firmware(self->plugin_uefi_capsule,
					      device_v20,
					      blob_fw,
					      progress,
					      FWUPD_INSTALL_FLAG_NO_SEARCH,
					      &error);
	g_assert_error(error, FWUPD_ERROR, FWUPD_ERROR_NOT_SUPPORTED);
	g_assert_false(ret);
	g_clear_error(&error);

	/* test override */
	ret =
	    fu_plugin_runner_write_firmware(self->plugin_uefi_capsule,
					    device_v20,
					    blob_fw,
					    progress,
					    FWUPD_INSTALL_FLAG_NO_SEARCH | FWUPD_INSTALL_FLAG_FORCE,
					    &error);
	g_assert_no_error(error);
	g_assert_true(ret);

	/* all */
	g_signal_handler_disconnect(self->plugin_uefi_capsule, added_id);
	g_signal_handler_disconnect(self->plugin_dell, register_id);
}

static void
fu_dell_plugin_dock_func(gconstpointer user_data)
{
	FuTest *self = (FuTest *)user_data;
	gboolean ret;
	guint32 out[4] = {0x0, 0x0, 0x0, 0x0};
	DOCK_UNION buf;
	DOCK_INFO *dock_info;
	g_autoptr(GError) error = NULL;
	g_autoptr(GPtrArray) devices = NULL;
	g_autoptr(FuProgress) progress = fu_progress_new(G_STRLOC);
	g_autoptr(FuUsbDevice) fake_usb_device = NULL;
	gulong added_id;
	gulong register_id;

	fake_usb_device = fu_usb_device_new(fu_plugin_get_context(self->plugin_dell), NULL);
	devices = g_ptr_array_new_with_free_func((GDestroyNotify)g_object_unref);
	added_id = g_signal_connect(FU_PLUGIN(self->plugin_uefi_capsule),
				    "device-added",
				    G_CALLBACK(_plugin_device_added_cb),
				    devices);
	register_id = g_signal_connect(FU_PLUGIN(self->plugin_dell),
				       "device-register",
				       G_CALLBACK(fu_engine_plugin_device_register_cb),
				       self->plugin_uefi_capsule);

	/* make sure bad device doesn't trigger this */
	fu_dell_plugin_inject_fake_data(self->plugin_dell,
					(guint32 *)&out,
					0x1234,
					0x4321,
					NULL,
					FALSE);
	ret = fu_dell_plugin_backend_device_added(self->plugin_dell,
						  FU_DEVICE(fake_usb_device),
						  progress,
						  &error);
	g_assert_false(ret);
	g_clear_error(&error);
	g_assert_cmpint(devices->len, ==, 0);

	/* inject a USB receiver matching correct VID/PID */
	out[0] = 0;
	out[1] = 0;
	fu_dell_plugin_inject_fake_data(self->plugin_dell,
					(guint32 *)&out,
					DOCK_NIC_VID,
					DOCK_NIC_PID,
					NULL,
					FALSE);
	ret = fu_dell_plugin_backend_device_added(self->plugin_dell,
						  FU_DEVICE(fake_usb_device),
						  progress,
						  &error);
	g_assert_true(ret);
	g_clear_error(&error);
	g_assert_cmpint(devices->len, ==, 0);

	/* inject valid TB16 dock w/ invalid flash pkg version */
	buf.record = g_malloc0(sizeof(DOCK_INFO_RECORD));
	dock_info = &buf.record->dock_info;
	buf.record->dock_info_header.dir_version = 1;
	buf.record->dock_info_header.dock_type = DOCK_TYPE_TB16;
	memcpy(dock_info->dock_description, "BME_Dock", 8);
	dock_info->flash_pkg_version = 0x00ffffff;
	dock_info->cable_type = CABLE_TYPE_TBT;
	dock_info->location = 2;
	dock_info->component_count = 4;
	dock_info->components[0].fw_version = 0x00ffffff;
	memcpy(dock_info->components[0].description,
	       "Dock1,EC,MIPS32,BME_Dock,0 :Query 2 0 2 1 0",
	       43);
	dock_info->components[1].fw_version = 0x10201;
	memcpy(dock_info->components[1].description, "Dock1,PC,TI,BME_Dock,0 :Query 2 1 0 1 0", 39);
	dock_info->components[2].fw_version = 0x10201;
	memcpy(dock_info->components[2].description, "Dock1,PC,TI,BME_Dock,1 :Query 2 1 0 1 1", 39);
	dock_info->components[3].fw_version = 0x00ffffff;
	memcpy(dock_info->components[3].description,
	       "Dock1,Cable,Cyp,TBT_Cable,0 :Query 2 2 2 3 0",
	       44);
	out[0] = 0;
	out[1] = 1;
	fu_dell_plugin_inject_fake_data(self->plugin_dell,
					(guint32 *)&out,
					DOCK_NIC_VID,
					DOCK_NIC_PID,
					buf.buf,
					FALSE);
	ret = fu_dell_plugin_backend_device_added(self->plugin_dell,
						  FU_DEVICE(fake_usb_device),
						  progress,
						  NULL);
	g_assert_true(ret);
	g_assert_cmpint(devices->len, ==, 4);
	g_ptr_array_set_size(devices, 0);
	g_free(buf.record);

	/* inject valid TB16 dock w/ older system EC */
	buf.record = g_malloc0(sizeof(DOCK_INFO_RECORD));
	dock_info = &buf.record->dock_info;
	buf.record->dock_info_header.dir_version = 1;
	buf.record->dock_info_header.dock_type = DOCK_TYPE_TB16;
	memcpy(dock_info->dock_description, "BME_Dock", 8);
	dock_info->flash_pkg_version = 0x43;
	dock_info->cable_type = CABLE_TYPE_TBT;
	dock_info->location = 2;
	dock_info->component_count = 4;
	dock_info->components[0].fw_version = 0xffffffff;
	memcpy(dock_info->components[0].description,
	       "Dock1,EC,MIPS32,BME_Dock,0 :Query 2 0 2 1 0",
	       43);
	dock_info->components[1].fw_version = 0x10211;
	memcpy(dock_info->components[1].description, "Dock1,PC,TI,BME_Dock,0 :Query 2 1 0 1 0", 39);
	dock_info->components[2].fw_version = 0x10212;
	memcpy(dock_info->components[2].description, "Dock1,PC,TI,BME_Dock,1 :Query 2 1 0 1 1", 39);
	dock_info->components[3].fw_version = 0xffffffff;
	memcpy(dock_info->components[3].description,
	       "Dock1,Cable,Cyp,TBT_Cable,0 :Query 2 2 2 3 0",
	       44);
	out[0] = 0;
	out[1] = 1;
	fu_dell_plugin_inject_fake_data(self->plugin_dell,
					(guint32 *)&out,
					DOCK_NIC_VID,
					DOCK_NIC_PID,
					buf.buf,
					FALSE);
	ret = fu_dell_plugin_backend_device_added(self->plugin_dell,
						  FU_DEVICE(fake_usb_device),
						  progress,
						  NULL);
	g_assert_true(ret);
	g_assert_cmpint(devices->len, ==, 3);
	g_ptr_array_set_size(devices, 0);
	g_free(buf.record);

	/* inject valid WD15 dock w/ invalid flash pkg version */
	buf.record = g_malloc0(sizeof(DOCK_INFO_RECORD));
	dock_info = &buf.record->dock_info;
	buf.record->dock_info_header.dir_version = 1;
	buf.record->dock_info_header.dock_type = DOCK_TYPE_WD15;
	memcpy(dock_info->dock_description, "IE_Dock", 7);
	dock_info->flash_pkg_version = 0x00ffffff;
	dock_info->cable_type = CABLE_TYPE_LEGACY;
	dock_info->location = 2;
	dock_info->component_count = 3;
	dock_info->components[0].fw_version = 0x00ffffff;
	memcpy(dock_info->components[0].description,
	       "Dock1,EC,MIPS32,IE_Dock,0 :Query 2 0 2 2 0",
	       42);
	dock_info->components[1].fw_version = 0x00ffffff;
	memcpy(dock_info->components[1].description, "Dock1,PC,TI,IE_Dock,0 :Query 2 1 0 2 0", 38);
	dock_info->components[2].fw_version = 0x00ffffff;
	memcpy(dock_info->components[2].description,
	       "Dock1,Cable,Cyp,IE_Cable,0 :Query 2 2 2 1 0",
	       43);
	out[0] = 0;
	out[1] = 1;
	fu_dell_plugin_inject_fake_data(self->plugin_dell,
					(guint32 *)&out,
					DOCK_NIC_VID,
					DOCK_NIC_PID,
					buf.buf,
					FALSE);
	ret = fu_dell_plugin_backend_device_added(self->plugin_dell,
						  FU_DEVICE(fake_usb_device),
						  progress,
						  &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	g_assert_cmpint(devices->len, ==, 3);
	g_ptr_array_set_size(devices, 0);
	g_free(buf.record);

	/* inject valid WD15 dock w/ older system EC */
	buf.record = g_malloc0(sizeof(DOCK_INFO_RECORD));
	dock_info = &buf.record->dock_info;
	buf.record->dock_info_header.dir_version = 1;
	buf.record->dock_info_header.dock_type = DOCK_TYPE_WD15;
	memcpy(dock_info->dock_description, "IE_Dock", 7);
	dock_info->flash_pkg_version = 0x43;
	dock_info->cable_type = CABLE_TYPE_LEGACY;
	dock_info->location = 2;
	dock_info->component_count = 3;
	dock_info->components[0].fw_version = 0xffffffff;
	memcpy(dock_info->components[0].description,
	       "Dock1,EC,MIPS32,IE_Dock,0 :Query 2 0 2 2 0",
	       42);
	dock_info->components[1].fw_version = 0x10108;
	memcpy(dock_info->components[1].description, "Dock1,PC,TI,IE_Dock,0 :Query 2 1 0 2 0", 38);
	dock_info->components[2].fw_version = 0xffffffff;
	memcpy(dock_info->components[2].description,
	       "Dock1,Cable,Cyp,IE_Cable,0 :Query 2 2 2 1 0",
	       43);
	out[0] = 0;
	out[1] = 1;
	fu_dell_plugin_inject_fake_data(self->plugin_dell,
					(guint32 *)&out,
					DOCK_NIC_VID,
					DOCK_NIC_PID,
					buf.buf,
					FALSE);
	ret = fu_dell_plugin_backend_device_added(self->plugin_dell,
						  FU_DEVICE(fake_usb_device),
						  progress,
						  &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	g_assert_cmpint(devices->len, ==, 2);
	g_ptr_array_set_size(devices, 0);
	g_free(buf.record);

	/* inject an invalid future dock */
	buf.record = g_malloc0(sizeof(DOCK_INFO_RECORD));
	dock_info = &buf.record->dock_info;
	buf.record->dock_info_header.dir_version = 1;
	buf.record->dock_info_header.dock_type = 50;
	memcpy(dock_info->dock_description, "Future!", 8);
	dock_info->flash_pkg_version = 0x00ffffff;
	dock_info->cable_type = CABLE_TYPE_UNIV;
	dock_info->location = 2;
	dock_info->component_count = 1;
	dock_info->components[0].fw_version = 0x00ffffff;
	memcpy(dock_info->components[0].description,
	       "Dock1,EC,MIPS32,FUT_Dock,0 :Query 2 0 2 2 0",
	       43);
	out[0] = 0;
	out[1] = 1;
	fu_dell_plugin_inject_fake_data(self->plugin_dell,
					(guint32 *)&out,
					DOCK_NIC_VID,
					DOCK_NIC_PID,
					buf.buf,
					FALSE);
	ret = fu_dell_plugin_backend_device_added(self->plugin_dell,
						  FU_DEVICE(fake_usb_device),
						  progress,
						  &error);
	g_assert_false(ret);
	g_assert_cmpint(devices->len, ==, 0);
	g_free(buf.record);

	/* all */
	g_signal_handler_disconnect(self->plugin_uefi_capsule, added_id);
	g_signal_handler_disconnect(self->plugin_dell, register_id);
}

static void
fu_test_self_init(FuTest *self)
{
	gboolean ret;
	g_autoptr(FuContext) ctx = fu_context_new();
	g_autoptr(FuProgress) progress = fu_progress_new(G_STRLOC);
	g_autoptr(GError) error = NULL;

	/* do not save silo */
	ret = fu_context_load_quirks(ctx, FU_QUIRKS_LOAD_FLAG_NO_CACHE, &error);
	g_assert_no_error(error);
	g_assert_true(ret);

	self->plugin_uefi_capsule =
	    fu_plugin_new_from_gtype(fu_uefi_capsule_plugin_get_type(), ctx);
	ret = fu_plugin_runner_startup(self->plugin_uefi_capsule, progress, &error);
	g_assert_no_error(error);
	g_assert_true(ret);

	self->plugin_dell = fu_plugin_new_from_gtype(fu_dell_plugin_get_type(), ctx);
	ret = fu_plugin_runner_startup(self->plugin_dell, progress, &error);
	g_assert_no_error(error);
	g_assert_true(ret);
}

static void
fu_test_self_free(FuTest *self)
{
	if (self->plugin_uefi_capsule != NULL)
		g_object_unref(self->plugin_uefi_capsule);
	if (self->plugin_dell != NULL)
		g_object_unref(self->plugin_dell);
	g_free(self);
}

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunused-function"
G_DEFINE_AUTOPTR_CLEANUP_FUNC(FuTest, fu_test_self_free)
#pragma clang diagnostic pop

int
main(int argc, char **argv)
{
	g_autofree gchar *sysfsdir = NULL;
	g_autofree gchar *testdatadir = NULL;
	g_autoptr(FuTest) self = g_new0(FuTest, 1);

	g_test_init(&argc, &argv, NULL);

	/* change path */
	testdatadir = g_test_build_filename(G_TEST_DIST, "tests", NULL);
	(void)g_setenv("FWUPD_SYSFSFWDIR", testdatadir, TRUE);
	(void)g_setenv("FWUPD_SYSFSFWATTRIBDIR", testdatadir, TRUE);

	/* change behavior */
	sysfsdir = fu_path_from_kind(FU_PATH_KIND_SYSFSDIR_FW);
	(void)g_setenv("FWUPD_UEFI_ESP_PATH", sysfsdir, TRUE);
	(void)g_setenv("FWUPD_UEFI_TEST", "1", TRUE);
	(void)g_setenv("FWUPD_DELL_FAKE_SMBIOS", "1", FALSE);

	/* only critical and error are fatal */
	g_log_set_fatal_mask(NULL, G_LOG_LEVEL_ERROR | G_LOG_LEVEL_CRITICAL);

	g_assert_cmpint(g_mkdir_with_parents("/tmp/fwupd-self-test/var/lib/fwupd", 0755), ==, 0);

	/* tests go here */
	fu_test_self_init(self);
	g_test_add_data_func("/fwupd/plugin{dell:tpm}", self, fu_dell_plugin_tpm_func);
	g_test_add_data_func("/fwupd/plugin{dell:dock}", self, fu_dell_plugin_dock_func);
	return g_test_run();
}
