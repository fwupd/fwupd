/*
 * Copyright (C) 2015-2017 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include <fwupd.h>
#include <glib/gstdio.h>
#include <stdlib.h>

#include "fu-device-private.h"
#include "fu-plugin-private.h"
#include "fu-plugin-dell.h"
#include "fu-plugin-vfuncs.h"
#include "fu-hash.h"

static FuDevice *
_find_device_by_id (GPtrArray *devices, const gchar *device_id)
{
	for (guint i = 0; i < devices->len; i++) {
		FuDevice *device = g_ptr_array_index (devices, i);
		if (g_strcmp0 (fu_device_get_id (device), device_id) == 0)
			return device;
	}
	return NULL;
}

static FuDevice *
_find_device_by_name (GPtrArray *devices, const gchar *device_id)
{
	for (guint i = 0; i < devices->len; i++) {
		FuDevice *device = g_ptr_array_index (devices, i);
		if (g_strcmp0 (fu_device_get_name (device), device_id) == 0)
			return device;
	}
	return NULL;
}

static void
_plugin_device_added_cb (FuPlugin *plugin, FuDevice *device, gpointer user_data)
{
	GPtrArray *devices = (GPtrArray *) user_data;
	if (fu_device_get_alternate_id (device) != NULL) {
		FuDevice *device_alt = _find_device_by_id (devices, fu_device_get_alternate_id (device));
		if (device_alt != NULL)
			fu_device_set_alternate (device, device_alt);
	}
	g_ptr_array_add (devices, g_object_ref (device));
}

static void
fu_engine_plugin_device_register_cb (FuPlugin *plugin_dell,
				     FuDevice *device,
				     gpointer user_data)
{
	FuPlugin *plugin_uefi = FU_PLUGIN (user_data);
	g_autofree gchar *dbg = fu_device_to_string (device);
	g_debug ("registering device: %s", dbg);
	fu_plugin_runner_device_register (plugin_uefi, device);
}

static void
fu_plugin_dell_tpm_func (void)
{
	FuDevice *device_v12;
	FuDevice *device_v20;
	const guint8 fw[30] = { 'F', 'W', 0x00 };
	gboolean ret;
	struct tpm_status tpm_out;
	const gchar *tpm_server_running = g_getenv ("TPM_SERVER_RUNNING");
	g_autofree gchar *pluginfn_uefi = NULL;
	g_autofree gchar *pluginfn_dell = NULL;
	g_autoptr(FuPlugin) plugin_dell = NULL;
	g_autoptr(FuPlugin) plugin_uefi = NULL;
	g_autoptr(GBytes) blob_fw = g_bytes_new_static (fw, sizeof(fw));
	g_autoptr(GError) error = NULL;
	g_autoptr(GPtrArray) devices = NULL;

	pluginfn_uefi = g_build_filename (PLUGINBUILDDIR, "..", "uefi",
					  "libfu_plugin_uefi." G_MODULE_SUFFIX,
					  NULL);
	pluginfn_dell = g_build_filename (PLUGINBUILDDIR,
					  "libfu_plugin_dell." G_MODULE_SUFFIX,
					  NULL);

	memset (&tpm_out, 0x0, sizeof(tpm_out));

	plugin_uefi = fu_plugin_new ();
	ret = fu_plugin_open (plugin_uefi, pluginfn_uefi, &error);
	g_assert_no_error (error);
	g_assert (ret);
	ret = fu_plugin_runner_startup (plugin_uefi, &error);
	g_assert_no_error (error);
	g_assert (ret);
	devices = g_ptr_array_new_with_free_func ((GDestroyNotify) g_object_unref);
	g_signal_connect (plugin_uefi, "device-added",
			  G_CALLBACK (_plugin_device_added_cb),
			  devices);

	plugin_dell = fu_plugin_new ();
	ret = fu_plugin_open (plugin_dell, pluginfn_dell, &error);
	g_assert_no_error (error);
	g_assert (ret);
	ret = fu_plugin_runner_startup (plugin_dell, &error);
	g_assert_no_error (error);
	g_assert (ret);
	g_signal_connect (plugin_dell, "device-register",
			  G_CALLBACK (fu_engine_plugin_device_register_cb),
			  plugin_uefi);
	ret = fu_plugin_runner_coldplug (plugin_dell, &error);
	g_assert_no_error (error);
	g_assert (ret);

#ifdef HAVE_GETUID
	if (tpm_server_running == NULL &&
	    (getuid () != 0 || geteuid () != 0)) {
		g_test_skip ("TPM tests require simulated TPM2.0 running or need root access with physical TPM");
		return;
	}
#endif

	/* inject fake data (no TPM) */
	tpm_out.ret = -2;
	fu_plugin_dell_inject_fake_data (plugin_dell,
					 (guint32 *) &tpm_out, 0, 0,
					 NULL, FALSE);
	ret = fu_plugin_dell_detect_tpm (plugin_dell, &error);
	g_assert_no_error (error);
	g_assert_false (ret);
	g_assert_cmpint (devices->len, ==, 0);

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
	fu_plugin_dell_inject_fake_data (plugin_dell,
					 (guint32 *) &tpm_out, 0, 0,
					 NULL, TRUE);
	ret = fu_plugin_dell_detect_tpm (plugin_dell, &error);
	g_assert_true (ret);
	g_assert_cmpint (devices->len, ==, 2);

	/* make sure 2.0 is locked */
	device_v20 = _find_device_by_name (devices, "TPM 2.0");
	g_assert_nonnull (device_v20);
	g_assert_true (fu_device_has_flag (device_v20, FWUPD_DEVICE_FLAG_LOCKED));

	/* make sure not allowed to flash 1.2 */
	device_v12 = _find_device_by_name (devices, "TPM 1.2");
	g_assert_nonnull (device_v12);
	g_assert_false (fu_device_has_flag (device_v12, FWUPD_DEVICE_FLAG_UPDATABLE));

	/* try to unlock 2.0 */
	ret = fu_plugin_runner_unlock (plugin_uefi, device_v20, &error);
	g_assert_error (error, FWUPD_ERROR, FWUPD_ERROR_NOT_SUPPORTED);
	g_assert_false (ret);
	g_clear_error (&error);

	/* cleanup */
	g_ptr_array_set_size (devices, 0);

	/* inject fake data:
	 * - that has flashes
	 * - owned
	 * - TPM 1.2
	 * dev will be the locked 2.0, alt will be the orig 1.2
	 */
	tpm_out.status = TPM_EN_MASK | TPM_OWN_MASK | (TPM_1_2_MODE << 8);
	tpm_out.flashes_left = 125;
	fu_plugin_dell_inject_fake_data (plugin_dell,
					 (guint32 *) &tpm_out, 0, 0,
					 NULL, TRUE);
	ret = fu_plugin_dell_detect_tpm (plugin_dell, &error);
	g_assert_no_error (error);
	g_assert (ret);

	/* make sure not allowed to flash 1.2 */
	device_v12 = _find_device_by_name (devices, "TPM 1.2");
	g_assert_nonnull (device_v12);
	g_assert_false (fu_device_has_flag (device_v12, FWUPD_DEVICE_FLAG_UPDATABLE));

	/* try to unlock 2.0 */
	device_v20 = _find_device_by_name (devices, "TPM 2.0");
	g_assert_nonnull (device_v20);
	ret = fu_plugin_runner_unlock (plugin_uefi, device_v20, &error);
	g_assert_error (error, FWUPD_ERROR, FWUPD_ERROR_NOT_SUPPORTED);
	g_assert_false (ret);
	g_clear_error (&error);

	/* cleanup */
	g_ptr_array_set_size (devices, 0);

	/* inject fake data:
	 * - that has flashes
	 * - not owned
	 * - TPM 1.2
	 * dev will be the locked 2.0, alt will be the orig 1.2
	 */
	tpm_out.status = TPM_EN_MASK | (TPM_1_2_MODE << 8);
	tpm_out.flashes_left = 125;
	fu_plugin_dell_inject_fake_data (plugin_dell,
					 (guint32 *) &tpm_out, 0, 0,
					 NULL, TRUE);
	ret = fu_plugin_dell_detect_tpm (plugin_dell, &error);
	g_assert_no_error (error);
	g_assert (ret);

	/* make sure allowed to flash 1.2 but not 2.0 */
	device_v12 = _find_device_by_name (devices, "TPM 1.2");
	g_assert_nonnull (device_v12);
	g_assert_true (fu_device_has_flag (device_v12, FWUPD_DEVICE_FLAG_UPDATABLE));
	device_v20 = _find_device_by_name (devices, "TPM 2.0");
	g_assert_nonnull (device_v20);
	g_assert_false (fu_device_has_flag (device_v20, FWUPD_DEVICE_FLAG_UPDATABLE));

	/* try to unlock 2.0 */
	ret = fu_plugin_runner_unlock (plugin_uefi, device_v20, &error);
	g_assert_no_error (error);
	g_assert (ret);

	/* make sure no longer allowed to flash 1.2 but can flash 2.0 */
	g_assert_false (fu_device_has_flag (device_v12, FWUPD_DEVICE_FLAG_UPDATABLE));
	g_assert_true (fu_device_has_flag (device_v20, FWUPD_DEVICE_FLAG_UPDATABLE));

	/* cleanup */
	g_ptr_array_set_size (devices, 0);

	/* inject fake data:
	 * - that has 1 flash left
	 * - not owned
	 * - TPM 2.0
	 * dev will be the locked 1.2, alt will be the orig 2.0
	 */
	tpm_out.status = TPM_EN_MASK | (TPM_2_0_MODE << 8);
	tpm_out.flashes_left = 1;
	fu_plugin_dell_inject_fake_data (plugin_dell,
					 (guint32 *) &tpm_out, 0, 0,
					 NULL, TRUE);
	ret = fu_plugin_dell_detect_tpm (plugin_dell, &error);
	g_assert_no_error (error);
	g_assert (ret);

	/* make sure allowed to flash 2.0 but not 1.2 */
	device_v20 = _find_device_by_name (devices, "TPM 2.0");
	g_assert_nonnull (device_v20);
	g_assert_true (fu_device_has_flag (device_v20, FWUPD_DEVICE_FLAG_UPDATABLE));
	device_v12 = _find_device_by_name (devices, "TPM 1.2");
	g_assert_nonnull (device_v12);
	g_assert_false (fu_device_has_flag (device_v12, FWUPD_DEVICE_FLAG_UPDATABLE));

	/* With one flash left we need an override */
	ret = fu_plugin_runner_update (plugin_uefi, device_v20, blob_fw,
				       FWUPD_INSTALL_FLAG_NONE, &error);
	g_assert_error (error, FWUPD_ERROR, FWUPD_ERROR_NOT_SUPPORTED);
	g_assert_false (ret);
	g_clear_error (&error);

	/* test override */
	g_test_expect_message ("FuPluginUefi", G_LOG_LEVEL_WARNING,
			       "missing or invalid embedded capsule header");
	ret = fu_plugin_runner_update (plugin_uefi, device_v20, blob_fw,
				       FWUPD_INSTALL_FLAG_FORCE, &error);
	g_test_assert_expected_messages ();
	g_assert_no_error (error);
	g_assert (ret);
}

static void
fu_plugin_dell_dock_func (void)
{
	gboolean ret;
	guint32 out[4] = { 0x0, 0x0, 0x0, 0x0 };
	DOCK_UNION buf;
	DOCK_INFO *dock_info;
	g_autofree gchar *pluginfn_uefi = NULL;
	g_autofree gchar *pluginfn_dell = NULL;
	g_autoptr(GError) error = NULL;
	g_autoptr(GPtrArray) devices = NULL;
	g_autoptr(FuPlugin) plugin_uefi = fu_plugin_new ();
	g_autoptr(FuPlugin) plugin_dell = fu_plugin_new ();

	pluginfn_uefi = g_build_filename (PLUGINBUILDDIR, "..", "uefi",
					  "libfu_plugin_uefi." G_MODULE_SUFFIX,
					  NULL);
	pluginfn_dell = g_build_filename (PLUGINBUILDDIR,
					  "libfu_plugin_dell." G_MODULE_SUFFIX,
					  NULL);

	ret = fu_plugin_open (plugin_uefi, pluginfn_uefi, &error);
	g_assert_no_error (error);
	g_assert (ret);
	ret = fu_plugin_runner_startup (plugin_uefi, &error);
	g_assert_no_error (error);
	g_assert (ret);
	devices = g_ptr_array_new_with_free_func ((GDestroyNotify) g_object_unref);
	g_signal_connect (plugin_uefi, "device-added",
			  G_CALLBACK (_plugin_device_added_cb),
			  devices);
	ret = fu_plugin_open (plugin_dell, pluginfn_dell, &error);
	g_assert_no_error (error);
	g_assert (ret);
	ret = fu_plugin_runner_startup (plugin_dell, &error);
	g_assert_no_error (error);
	g_assert (ret);
	g_signal_connect (plugin_dell, "device-register",
			  G_CALLBACK (fu_engine_plugin_device_register_cb),
			  plugin_uefi);
	ret = fu_plugin_runner_coldplug (plugin_dell, &error);
	g_assert_no_error (error);
	g_assert (ret);

	/* make sure bad device doesn't trigger this */
	fu_plugin_dell_inject_fake_data (plugin_dell,
					   (guint32 *) &out,
					   0x1234, 0x4321, NULL, FALSE);
	ret = fu_plugin_usb_device_added (plugin_dell, NULL, &error);
	g_assert_false (ret);
	g_clear_error (&error);
	g_assert_cmpint (devices->len, ==, 0);

	/* inject a USB dongle matching correct VID/PID */
	out[0] = 0;
	out[1] = 0;
	fu_plugin_dell_inject_fake_data (plugin_dell,
					   (guint32 *) &out,
					   DOCK_NIC_VID, DOCK_NIC_PID,
					   NULL, FALSE);
	ret = fu_plugin_usb_device_added (plugin_dell, NULL, &error);
	g_assert_true (ret);
	g_clear_error (&error);
	g_assert_cmpint (devices->len, ==, 0);

	/* inject valid TB16 dock w/ invalid flash pkg version */
	buf.record = g_malloc0 (sizeof(DOCK_INFO_RECORD));
	dock_info = &buf.record->dock_info;
	buf.record->dock_info_header.dir_version = 1;
	buf.record->dock_info_header.dock_type = DOCK_TYPE_TB16;
	memcpy (dock_info->dock_description,
		"BME_Dock", 8);
	dock_info->flash_pkg_version = 0x00ffffff;
	dock_info->cable_type = CABLE_TYPE_TBT;
	dock_info->location = 2;
	dock_info->component_count = 4;
	dock_info->components[0].fw_version = 0x00ffffff;
	memcpy (dock_info->components[0].description,
		"Dock1,EC,MIPS32,BME_Dock,0 :Query 2 0 2 1 0", 43);
	dock_info->components[1].fw_version = 0x10201;
	memcpy (dock_info->components[1].description,
		"Dock1,PC,TI,BME_Dock,0 :Query 2 1 0 1 0", 39);
	dock_info->components[2].fw_version = 0x10201;
	memcpy (dock_info->components[2].description,
		"Dock1,PC,TI,BME_Dock,1 :Query 2 1 0 1 1", 39);
	dock_info->components[3].fw_version = 0x00ffffff;
	memcpy (dock_info->components[3].description,
		"Dock1,Cable,Cyp,TBT_Cable,0 :Query 2 2 2 3 0", 44);
	out[0] = 0;
	out[1] = 1;
	fu_plugin_dell_inject_fake_data (plugin_dell,
					   (guint32 *) &out,
					   DOCK_NIC_VID, DOCK_NIC_PID,
					   buf.buf, FALSE);
	ret = fu_plugin_usb_device_added (plugin_dell, NULL, NULL);
	g_assert (ret);
	g_assert_cmpint (devices->len, ==, 4);
	g_ptr_array_set_size (devices, 0);
	g_free (buf.record);

	/* inject valid TB16 dock w/ older system EC */
	buf.record = g_malloc0 (sizeof(DOCK_INFO_RECORD));
	dock_info = &buf.record->dock_info;
	buf.record->dock_info_header.dir_version = 1;
	buf.record->dock_info_header.dock_type = DOCK_TYPE_TB16;
	memcpy (dock_info->dock_description,
		"BME_Dock", 8);
	dock_info->flash_pkg_version = 0x43;
	dock_info->cable_type = CABLE_TYPE_TBT;
	dock_info->location = 2;
	dock_info->component_count = 4;
	dock_info->components[0].fw_version = 0xffffffff;
	memcpy (dock_info->components[0].description,
		"Dock1,EC,MIPS32,BME_Dock,0 :Query 2 0 2 1 0", 43);
	dock_info->components[1].fw_version = 0x10211;
	memcpy (dock_info->components[1].description,
		"Dock1,PC,TI,BME_Dock,0 :Query 2 1 0 1 0", 39);
	dock_info->components[2].fw_version = 0x10212;
	memcpy (dock_info->components[2].description,
		"Dock1,PC,TI,BME_Dock,1 :Query 2 1 0 1 1", 39);
	dock_info->components[3].fw_version = 0xffffffff;
	memcpy (dock_info->components[3].description,
		"Dock1,Cable,Cyp,TBT_Cable,0 :Query 2 2 2 3 0", 44);
	out[0] = 0;
	out[1] = 1;
	fu_plugin_dell_inject_fake_data (plugin_dell,
					   (guint32 *) &out,
					   DOCK_NIC_VID, DOCK_NIC_PID,
					   buf.buf, FALSE);
	ret = fu_plugin_usb_device_added (plugin_dell, NULL, NULL);
	g_assert (ret);
	g_assert_cmpint (devices->len, ==, 3);
	g_ptr_array_set_size (devices, 0);
	g_free (buf.record);

	/* inject valid WD15 dock w/ invalid flash pkg version */
	buf.record = g_malloc0 (sizeof(DOCK_INFO_RECORD));
	dock_info = &buf.record->dock_info;
	buf.record->dock_info_header.dir_version = 1;
	buf.record->dock_info_header.dock_type = DOCK_TYPE_WD15;
	memcpy (dock_info->dock_description,
		"IE_Dock", 7);
	dock_info->flash_pkg_version = 0x00ffffff;
	dock_info->cable_type = CABLE_TYPE_LEGACY;
	dock_info->location = 2;
	dock_info->component_count = 3;
	dock_info->components[0].fw_version = 0x00ffffff;
	memcpy (dock_info->components[0].description,
		"Dock1,EC,MIPS32,IE_Dock,0 :Query 2 0 2 2 0", 42);
	dock_info->components[1].fw_version = 0x00ffffff;
	memcpy (dock_info->components[1].description,
		"Dock1,PC,TI,IE_Dock,0 :Query 2 1 0 2 0", 38);
	dock_info->components[2].fw_version = 0x00ffffff;
	memcpy (dock_info->components[2].description,
		"Dock1,Cable,Cyp,IE_Cable,0 :Query 2 2 2 1 0", 43);
	out[0] = 0;
	out[1] = 1;
	fu_plugin_dell_inject_fake_data (plugin_dell,
					   (guint32 *) &out,
					   DOCK_NIC_VID, DOCK_NIC_PID,
					   buf.buf, FALSE);
	ret = fu_plugin_usb_device_added (plugin_dell, NULL, &error);
	g_assert (ret);
	g_assert_no_error (error);
	g_assert_cmpint (devices->len, ==, 3);
	g_ptr_array_set_size (devices, 0);
	g_free (buf.record);

	/* inject valid WD15 dock w/ older system EC */
	buf.record = g_malloc0 (sizeof(DOCK_INFO_RECORD));
	dock_info = &buf.record->dock_info;
	buf.record->dock_info_header.dir_version = 1;
	buf.record->dock_info_header.dock_type = DOCK_TYPE_WD15;
	memcpy (dock_info->dock_description,
		"IE_Dock", 7);
	dock_info->flash_pkg_version = 0x43;
	dock_info->cable_type = CABLE_TYPE_LEGACY;
	dock_info->location = 2;
	dock_info->component_count = 3;
	dock_info->components[0].fw_version = 0xffffffff;
	memcpy (dock_info->components[0].description,
		"Dock1,EC,MIPS32,IE_Dock,0 :Query 2 0 2 2 0", 42);
	dock_info->components[1].fw_version = 0x10108;
	memcpy (dock_info->components[1].description,
		"Dock1,PC,TI,IE_Dock,0 :Query 2 1 0 2 0", 38);
	dock_info->components[2].fw_version = 0xffffffff;
	memcpy (dock_info->components[2].description,
		"Dock1,Cable,Cyp,IE_Cable,0 :Query 2 2 2 1 0", 43);
	out[0] = 0;
	out[1] = 1;
	fu_plugin_dell_inject_fake_data (plugin_dell,
					 (guint32 *) &out,
					 DOCK_NIC_VID, DOCK_NIC_PID,
					 buf.buf, FALSE);
	ret = fu_plugin_usb_device_added (plugin_dell, NULL, &error);
	g_assert (ret);
	g_assert_no_error (error);
	g_assert_cmpint (devices->len, ==, 2);
	g_ptr_array_set_size (devices, 0);
	g_free (buf.record);

	/* inject an invalid future dock */
	buf.record = g_malloc0 (sizeof(DOCK_INFO_RECORD));
	dock_info = &buf.record->dock_info;
	buf.record->dock_info_header.dir_version = 1;
	buf.record->dock_info_header.dock_type = 50;
	memcpy (dock_info->dock_description,
		"Future!", 8);
	dock_info->flash_pkg_version = 0x00ffffff;
	dock_info->cable_type = CABLE_TYPE_UNIV;
	dock_info->location = 2;
	dock_info->component_count = 1;
	dock_info->components[0].fw_version = 0x00ffffff;
	memcpy (dock_info->components[0].description,
		"Dock1,EC,MIPS32,FUT_Dock,0 :Query 2 0 2 2 0", 43);
	out[0] = 0;
	out[1] = 1;
	fu_plugin_dell_inject_fake_data (plugin_dell,
					 (guint32 *) &out,
					 DOCK_NIC_VID, DOCK_NIC_PID,
					 buf.buf, FALSE);
	ret = fu_plugin_usb_device_added (plugin_dell, NULL, &error);
	g_assert_false (ret);
	g_assert_cmpint (devices->len, ==, 0);
	g_free (buf.record);
}

int
main (int argc, char **argv)
{
	g_autofree gchar *sysfsdir = NULL;
	g_test_init (&argc, &argv, NULL);

	/* change path */
	g_setenv ("FWUPD_SYSFSFWDIR", TESTDATADIR, TRUE);

	/* change behaviour */
	sysfsdir = fu_common_get_path (FU_PATH_KIND_SYSFSDIR_FW);
	g_setenv ("FWUPD_UEFI_ESP_PATH", sysfsdir, TRUE);
	g_setenv ("FWUPD_UEFI_TEST", "1", TRUE);
	g_setenv ("FWUPD_DELL_FAKE_SMBIOS", "1", FALSE);

	/* only critical and error are fatal */
	g_log_set_fatal_mask (NULL, G_LOG_LEVEL_ERROR | G_LOG_LEVEL_CRITICAL);

	g_assert_cmpint (g_mkdir_with_parents ("/tmp/fwupd-self-test/var/lib/fwupd", 0755), ==, 0);

	/* tests go here */
	g_test_add_func ("/fwupd/plugin{dell:tpm}", fu_plugin_dell_tpm_func);
	g_test_add_func ("/fwupd/plugin{dell:dock}", fu_plugin_dell_dock_func);
	return g_test_run ();
}
