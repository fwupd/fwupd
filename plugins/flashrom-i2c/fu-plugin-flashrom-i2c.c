/* Copyright 2020 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <fu-plugin.h>
#include <fu-plugin-vfuncs.h>
#include <string.h>
#include <unistd.h>

#include "config.h"
#include "fu-hash.h"
#include "fu-i2c-device.h"

#define DEVICE_NAME_PREFIX "i2c-"
#define HID_LENGTH 8
#define I2C_PATH_REGEX "^i2c-[0-9]+$"
#define DEVICE_GUID_SOURCE_FORMAT "FLASHROM-I2C\\VEN_%s&amp;DEV_%s"
#define SYMLINK_LEN_MAX 128

static gboolean i2c_match_regex (const char *target, GError **error)
{
	GRegex *regex = NULL;
	gboolean result;

	regex = g_regex_new (I2C_PATH_REGEX, 0, 0, error);
	if (regex == NULL)
		return FALSE;

	result = g_regex_match_all_full (regex, target, -1, 0, 0, NULL, error);
	g_regex_unref(regex);
	return result;
}

static gint get_i2c_bus_number_from_path (gchar *device_path, GError **error)
{
	gint bus_no = -1;
	g_autofree gchar *device_symlink = NULL;
	g_auto(GStrv) names = NULL;

	device_symlink = g_file_read_link (device_path, error);
	if (device_symlink == NULL)
		return bus_no;

	names = g_strsplit (device_symlink, "/", -1);
	for (guint i = 0; names[i] != NULL; i++) {
		if (i2c_match_regex (names[i], error)) {
			bus_no = g_ascii_strtoll (
				&names[i][strlen (DEVICE_NAME_PREFIX)],
				NULL,
				10);
			break;
		}
	}

	return bus_no;
}

static gchar *get_i2c_device_guid (const gchar *pid, const gchar *vid)
{
	gchar *guid_source = NULL;
	guid_source = g_strdup_printf (DEVICE_GUID_SOURCE_FORMAT, pid, vid);
	return fwupd_guid_hash_string (guid_source);
}

static gboolean add_i2c_device (FuPlugin *plugin, const gchar *i2c_device_dir,
				const gchar *i2c_name, GError **error)
{
	gint bus_no;
	const gchar *quirk_programmer_name;
	const gchar *quirk_device_name;
	const gchar *quirk_device_protocol;
	const gchar *quirk_vendor_name;
	g_autofree gchar *hw_id = NULL;
	g_autofree gchar *quirk_key = NULL;
	g_autofree gchar *device_path = NULL;
	g_autofree gchar *device_vid = NULL;
	g_autofree gchar *device_pid = NULL;
	g_autofree gchar *device_guid = NULL;
	g_autofree gchar *vendor_id = NULL;
	g_autofree gchar *physical_id = NULL;
	g_autoptr (FuI2cDevice) dev = NULL;

	hw_id = g_strndup (&i2c_name[strlen (DEVICE_NAME_PREFIX)],
			     HID_LENGTH);
	quirk_key = g_strdup_printf ("HwId=%s", hw_id);

	quirk_programmer_name = fu_plugin_lookup_quirk_by_id (
		plugin, quirk_key, PROGRAMMER_NAME);
	quirk_device_name = fu_plugin_lookup_quirk_by_id (
		plugin, quirk_key, DEVICE_NAME);
	quirk_device_protocol = fu_plugin_lookup_quirk_by_id (
		plugin, quirk_key, DEVICE_PROTOCOL);
	quirk_vendor_name = fu_plugin_lookup_quirk_by_id (
                plugin, quirk_key, DEVICE_VENDOR_NAME);
	/* Add devices with quirk configuration only. */
	if (quirk_programmer_name == NULL)
		return TRUE;

	device_path = g_build_filename (
		i2c_device_dir, i2c_name, NULL);
	bus_no = get_i2c_bus_number_from_path (device_path, error);
	if (bus_no == -1) {
		g_set_error (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_INTERNAL,
			     "%s failed to get bus number for " \
			     "device under %s.",
			     __func__,
			     device_path);
		return FALSE;
	}

	device_vid = g_strndup (hw_id, HID_LENGTH / 2);
	device_pid = g_strndup (&hw_id[HID_LENGTH / 2], HID_LENGTH / 2);
	device_guid = get_i2c_device_guid (device_vid, device_pid);
	if (device_guid == NULL) {
		g_set_error (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_INTERNAL,
			     "%s failed to generate guid.",
			     __func__);
		return FALSE;
	}

	vendor_id = g_strdup_printf ("I2C:%s", device_vid);
	physical_id = g_strdup_printf ("DEVNAME=%s", device_path);
	dev = g_object_new (FU_TYPE_I2C_DEVICE, NULL);

	fu_device_add_guid (FU_DEVICE (dev), device_guid);
	fu_device_set_vendor (FU_DEVICE (dev), quirk_vendor_name);
	fu_device_set_vendor_id (FU_DEVICE (dev), vendor_id);
	fu_device_set_version_format (FU_DEVICE (dev),
				      FWUPD_VERSION_FORMAT_PAIR);
	/* TODO(b/154178623): Get the real version number using flashrom. */
	fu_device_set_version (FU_DEVICE (dev), "0.0");
	fu_device_set_name (FU_DEVICE (dev), quirk_device_name);
	fu_device_set_protocol (FU_DEVICE (dev), quirk_device_protocol);
	fu_device_set_physical_id (FU_DEVICE (dev), physical_id);
	fu_device_set_metadata_integer (FU_DEVICE (dev), PORT_NAME, bus_no);
	fu_device_set_metadata (FU_DEVICE (dev), PROGRAMMER_NAME,
		quirk_programmer_name);
	fu_plugin_device_add (plugin, FU_DEVICE (dev));
	return TRUE;
}

void fu_plugin_init (FuPlugin *plugin)
{
	fu_plugin_set_build_hash (plugin, FU_BUILD_HASH);
	fu_plugin_set_device_gtype (plugin, FU_TYPE_I2C_DEVICE);
}

gboolean fu_plugin_coldplug (FuPlugin *plugin, GError **error)
{
	g_autofree gchar *i2c_device_dir =
		fu_common_get_path (FU_PATH_KIND_I2C_DEVICES);
	GDir *dir;
	const gchar *ent_name;

	dir = g_dir_open (i2c_device_dir, 0, error);
	if (dir == NULL) {
		g_set_error (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_INTERNAL,
			     "%s failed to open directory: %s.",
			     __func__,
			     i2c_device_dir);
		return FALSE;
	}

	while ((ent_name = g_dir_read_name (dir)) != NULL) {
		if (strlen (ent_name) <
		    strlen (DEVICE_NAME_PREFIX) + HID_LENGTH)
			continue;

		if (add_i2c_device (plugin, i2c_device_dir,
				    ent_name, error) == FALSE) {
			return FALSE;
		}
	}

	g_dir_close (dir);
	return TRUE;
}

gboolean fu_plugin_update (FuPlugin *plugin,
			   FuDevice *dev,
			   GBytes *blob_fw,
			   FwupdInstallFlags flags,
			   GError **error)
{
	FuDevice *parent = fu_device_get_parent (dev);
	g_autoptr (FuDeviceLocker) locker = NULL;
	locker = fu_device_locker_new (parent != NULL ? parent : dev, error);
	if (locker == NULL)
		return FALSE;
	return fu_device_write_firmware (dev, blob_fw, flags, error);
}
