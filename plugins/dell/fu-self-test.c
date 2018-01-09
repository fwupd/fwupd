/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2015-2017 Richard Hughes <richard@hughsie.com>
 *
 * Licensed under the GNU General Public License Version 2
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#include "config.h"

#include <fwupd.h>
#include <glib-object.h>
#include <glib/gstdio.h>
#include <stdlib.h>

#include "fu-plugin-private.h"
#include "fu-plugin-dell.h"

static void
_plugin_device_added_cb (FuPlugin *plugin, FuDevice *device, gpointer user_data)
{
	FuDevice **dev = (FuDevice **) user_data;
	g_set_object (dev, device);
}

static void
fu_plugin_dell_tpm_func (void)
{
	gboolean ret;
	struct tpm_status tpm_out;
	FuDevice *device_alt = NULL;
	g_autoptr(GError) error = NULL;
	g_autoptr(FuDevice) device = NULL;
	g_autoptr(FuPlugin) plugin = NULL;

	memset (&tpm_out, 0x0, sizeof(tpm_out));

	g_setenv ("FWUPD_DELL_FAKE_SMBIOS", "1", FALSE);
	plugin = fu_plugin_new ();
	ret = fu_plugin_open (plugin, PLUGINBUILDDIR "/libfu_plugin_dell.so", &error);
	g_assert_no_error (error);
	g_assert (ret);
	ret = fu_plugin_runner_startup (plugin, &error);
	g_assert_no_error (error);
	g_assert (ret);
	ret = fu_plugin_runner_coldplug(plugin, &error);
	g_signal_connect (plugin, "device-added",
			  G_CALLBACK (_plugin_device_added_cb),
			  &device);
	g_assert_no_error (error);
	g_assert (ret);

	/* inject fake data (no TPM) */
	tpm_out.ret = -2;
	fu_plugin_dell_inject_fake_data (plugin,
					 (guint32 *) &tpm_out, 0, 0,
					 NULL, FALSE);
	ret = fu_plugin_dell_detect_tpm (plugin, &error);
	g_assert_no_error (error);
	g_assert (!ret);

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
	fu_plugin_dell_inject_fake_data (plugin,
					 (guint32 *) &tpm_out, 0, 0,
					 NULL, TRUE);
	ret = fu_plugin_dell_detect_tpm (plugin, &error);
	device_alt = fu_device_get_alternate (device);
	g_assert_no_error (error);
	g_assert (ret);
	g_assert (device != NULL);
	g_assert (device_alt != NULL);

	/* make sure 2.0 is locked */
	g_assert_true (fu_device_has_flag (device, FWUPD_DEVICE_FLAG_LOCKED));

	/* make sure not allowed to flash 1.2 */
	g_assert_false (fu_device_has_flag (device_alt, FWUPD_DEVICE_FLAG_UPDATABLE));

	/* try to unlock 2.0 */
	ret = fu_plugin_runner_unlock (plugin, device, &error);
	g_assert_error (error, FWUPD_ERROR, FWUPD_ERROR_NOT_SUPPORTED);
	g_assert (!ret);
	g_clear_error (&error);

	/* cleanup */
	fu_plugin_device_remove (plugin, device_alt);
	fu_plugin_device_remove (plugin, device);
	g_clear_object (&device);

	/* inject fake data:
	 * - that hasflashes
	 * - owned
	 * - TPM 1.2
	 * dev will be the locked 2.0, alt will be the orig 1.2
	 */
	tpm_out.status = TPM_EN_MASK | TPM_OWN_MASK | (TPM_1_2_MODE << 8);
	tpm_out.flashes_left = 125;
	fu_plugin_dell_inject_fake_data (plugin,
					 (guint32 *) &tpm_out, 0, 0,
					 NULL, TRUE);
	ret = fu_plugin_dell_detect_tpm (plugin, &error);
	device_alt = fu_device_get_alternate (device);
	g_assert_no_error (error);
	g_assert (ret);
	g_assert (device != NULL);
	g_assert (device_alt != NULL);

	/* make sure not allowed to flash 1.2 */
	g_assert_false (fu_device_has_flag (device_alt, FWUPD_DEVICE_FLAG_UPDATABLE));

	/* try to unlock 2.0 */
	ret = fu_plugin_runner_unlock (plugin, device, &error);
	g_assert_error (error, FWUPD_ERROR, FWUPD_ERROR_NOT_SUPPORTED);
	g_assert (!ret);
	g_clear_error (&error);

	/* cleanup */
	fu_plugin_device_remove (plugin, device_alt);
	fu_plugin_device_remove (plugin, device);
	g_clear_object (&device);

	/* inject fake data:
	 * - that has flashes
	 * - not owned
	 * - TPM 1.2
	 * dev will be the locked 2.0, alt will be the orig 1.2
	 */
	tpm_out.status = TPM_EN_MASK | (TPM_1_2_MODE << 8);
	tpm_out.flashes_left = 125;
	fu_plugin_dell_inject_fake_data (plugin,
					 (guint32 *) &tpm_out, 0, 0,
					 NULL, TRUE);
	ret = fu_plugin_dell_detect_tpm (plugin, &error);
	device_alt = fu_device_get_alternate (device);
	g_assert_no_error (error);
	g_assert (ret);
	g_assert (device != NULL);
	g_assert (device_alt != NULL);

	/* make sure allowed to flash 1.2 but not 2.0 */
	g_assert_true (fu_device_has_flag (device_alt, FWUPD_DEVICE_FLAG_UPDATABLE));
	g_assert_false (fu_device_has_flag (device, FWUPD_DEVICE_FLAG_UPDATABLE));

	/* try to unlock 2.0 */
	ret = fu_plugin_runner_unlock (plugin, device, &error);
	g_assert_no_error (error);
	g_assert (ret);

	/* make sure no longer allowed to flash 1.2 but can flash 2.0 */
	g_assert_false (fu_device_has_flag (device_alt, FWUPD_DEVICE_FLAG_UPDATABLE));
	g_assert_true (fu_device_has_flag (device, FWUPD_DEVICE_FLAG_UPDATABLE));

	/* cleanup */
	fu_plugin_device_remove (plugin, device_alt);
	fu_plugin_device_remove (plugin, device);
	g_clear_object (&device);

	/* inject fake data:
	 * - that has 1 flash left
	 * - not owned
	 * - TPM 2.0
	 * dev will be the locked 1.2, alt will be the orig 2.0
	 */
	tpm_out.status = TPM_EN_MASK | (TPM_2_0_MODE << 8);
	tpm_out.flashes_left = 1;
	fu_plugin_dell_inject_fake_data (plugin,
					 (guint32 *) &tpm_out, 0, 0,
					 NULL, TRUE);
	ret = fu_plugin_dell_detect_tpm (plugin, &error);
	device_alt = fu_device_get_alternate (device);
	g_assert_no_error (error);
	g_assert (ret);
	g_assert (device != NULL);
	g_assert (device_alt != NULL);

	/* make sure allowed to flash 2.0 but not 1.2 */
	g_assert_true (fu_device_has_flag (device_alt, FWUPD_DEVICE_FLAG_UPDATABLE));
	g_assert_false (fu_device_has_flag (device, FWUPD_DEVICE_FLAG_UPDATABLE));

	/* With one flash left we need an override */
	ret = fu_plugin_runner_update (plugin, device_alt, NULL, NULL,
				  FWUPD_INSTALL_FLAG_NONE, &error);
	g_assert (!ret);
	g_assert_error (error, FWUPD_ERROR, FWUPD_ERROR_NOT_SUPPORTED);
	g_clear_error (&error);

	/* test override */
	ret = fu_plugin_runner_update (plugin, device_alt, NULL, NULL,
				  FWUPD_INSTALL_FLAG_FORCE, &error);
	g_assert (ret);
	g_assert_no_error (error);

	/* cleanup */
	fu_plugin_device_remove (plugin, device_alt);
	fu_plugin_device_remove (plugin, device);
	g_clear_object (&device);
}

static void
fu_plugin_dell_dock_func (void)
{
	gboolean ret;
	guint32 out[4] = { 0x0, 0x0, 0x0, 0x0 };
	DOCK_UNION buf;
	DOCK_INFO *dock_info;
	g_autoptr(GError) error = NULL;
	g_autoptr(FuDevice) device = NULL;
	g_autoptr(FuPlugin) plugin = NULL;

	g_setenv ("FWUPD_DELL_FAKE_SMBIOS", "1", FALSE);
	plugin = fu_plugin_new ();
	ret = fu_plugin_open (plugin, PLUGINBUILDDIR "/libfu_plugin_dell.so", &error);
	g_assert_no_error (error);
	g_assert (ret);
	ret = fu_plugin_runner_startup (plugin, &error);
	g_assert_no_error (error);
	g_assert (ret);
	g_signal_connect (plugin, "device-added",
			  G_CALLBACK (_plugin_device_added_cb),
			  &device);
	ret = fu_plugin_runner_coldplug (plugin, &error);
	g_assert_no_error (error);
	g_assert (ret);

	/* make sure bad device doesn't trigger this */
	fu_plugin_dell_inject_fake_data (plugin,
					   (guint32 *) &out,
					   0x1234, 0x4321, NULL, FALSE);
	fu_plugin_dell_device_added_cb (NULL, NULL, plugin);
	g_assert (device == NULL);

	/* inject a USB dongle matching correct VID/PID */
	out[0] = 0;
	out[1] = 0;
	fu_plugin_dell_inject_fake_data (plugin,
					   (guint32 *) &out,
					   DOCK_NIC_VID, DOCK_NIC_PID,
					   NULL, FALSE);
	fu_plugin_dell_device_added_cb (NULL, NULL, plugin);
	g_assert (device == NULL);

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
	fu_plugin_dell_inject_fake_data (plugin,
					   (guint32 *) &out,
					   DOCK_NIC_VID, DOCK_NIC_PID,
					   buf.buf, FALSE);
	fu_plugin_dell_device_added_cb (NULL, NULL,
					  plugin);
	g_assert (device != NULL);
	g_clear_object (&device);
	g_free (buf.record);
	fu_plugin_dell_device_removed_cb (NULL, NULL,
					    plugin);

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
	fu_plugin_dell_inject_fake_data (plugin,
					   (guint32 *) &out,
					   DOCK_NIC_VID, DOCK_NIC_PID,
					   buf.buf, FALSE);
	fu_plugin_dell_device_added_cb (NULL, NULL,
					  plugin);
	g_assert (device != NULL);
	g_clear_object (&device);
	g_free (buf.record);
	fu_plugin_dell_device_removed_cb (NULL, NULL,
					    plugin);


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
	fu_plugin_dell_inject_fake_data (plugin,
					   (guint32 *) &out,
					   DOCK_NIC_VID, DOCK_NIC_PID,
					   buf.buf, FALSE);
	fu_plugin_dell_device_added_cb (NULL, NULL,
					  plugin);
	g_assert (device != NULL);
	g_clear_object (&device);
	g_free (buf.record);
	fu_plugin_dell_device_removed_cb (NULL, NULL,
					    plugin);


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
	fu_plugin_dell_inject_fake_data (plugin,
					 (guint32 *) &out,
					 DOCK_NIC_VID, DOCK_NIC_PID,
					 buf.buf, FALSE);
	fu_plugin_dell_device_added_cb (NULL, NULL,
					  plugin);
	g_assert (device != NULL);
	g_clear_object (&device);
	g_free (buf.record);
	fu_plugin_dell_device_removed_cb (NULL, NULL,
					    plugin);

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
	fu_plugin_dell_inject_fake_data (plugin,
					 (guint32 *) &out,
					 DOCK_NIC_VID, DOCK_NIC_PID,
					 buf.buf, FALSE);
	fu_plugin_dell_device_added_cb (NULL, NULL,
					  plugin);
	g_assert (device == NULL);
	g_free (buf.record);
}

int
main (int argc, char **argv)
{
	g_test_init (&argc, &argv, NULL);

	/* only critical and error are fatal */
	g_log_set_fatal_mask (NULL, G_LOG_LEVEL_ERROR | G_LOG_LEVEL_CRITICAL);

	g_assert_cmpint (g_mkdir_with_parents ("/tmp/fwupd-self-test/var/lib/fwupd", 0755), ==, 0);

	/* tests go here */
	g_test_add_func ("/fwupd/plugin{dell:tpm}", fu_plugin_dell_tpm_func);
	g_test_add_func ("/fwupd/plugin{dell:dock}", fu_plugin_dell_dock_func);
	return g_test_run ();
}
