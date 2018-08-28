/*
 * Copyright (C) 2017 Christian J. Kellner <christian@kellner.me>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <errno.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>

#include <gio/gio.h>
#include <glib.h>

#include "fu-plugin-thunderbolt.h"
#include "fu-plugin-vfuncs.h"
#include "fu-device-metadata.h"
#include "fu-thunderbolt-image.h"

#ifndef HAVE_GUDEV_232
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunused-function"
G_DEFINE_AUTOPTR_CLEANUP_FUNC(GUdevDevice, g_object_unref)
#pragma clang diagnostic pop
#endif

#define TBT_NVM_RETRY_TIMEOUT	200 /* ms */

typedef void (*UEventNotify) (FuPlugin	  *plugin,
			      GUdevDevice *udevice,
			      const gchar *action,
			      gpointer     user_data);

struct FuPluginData {
	GUdevClient   *udev;

	/* in the case we are updating */
	UEventNotify   update_notify;
	gpointer       update_data;

	/* the timeout we wait for the device
	 * to be updated to re-appear, in ms.
	 * defaults to:
	 *  FU_PLUGIN_THUNDERBOLT_UPDATE_TIMEOUT_MS */
	guint timeout;
};

static gchar *
fu_plugin_thunderbolt_gen_id_from_syspath (const gchar *syspath)
{
	gchar *id;
	id = g_strdup_printf ("tbt-%s", syspath);
	g_strdelimit (id, "/:.-", '_');
	return id;
}


static gchar *
fu_plugin_thunderbolt_gen_id (GUdevDevice *device)
{
	const gchar *syspath = g_udev_device_get_sysfs_path (device);
	return fu_plugin_thunderbolt_gen_id_from_syspath (syspath);
}

static guint64
udev_device_get_sysattr_guint64 (GUdevDevice *device,
				 const gchar *name,
				 GError **error)
{
	const gchar *sysfs;
	guint64 val;

	sysfs = g_udev_device_get_sysfs_attr (device, name);
	if (sysfs == NULL) {
		g_set_error (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_INTERNAL,
			     "failed get id %s for %s", name, sysfs);
		return 0x0;
	}

	val = g_ascii_strtoull (sysfs, NULL, 16);
	if (val == 0x0) {
		g_set_error (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_INTERNAL,
			     "failed to parse %s", sysfs);
		return 0x0;
	}

	return val;
}

static guint16
fu_plugin_thunderbolt_udev_get_id (GUdevDevice *device,
				   const gchar *name,
				   GError **error)
{

	guint64 id;

	id = udev_device_get_sysattr_guint64 (device, name, error);
	if (id == 0x0)
		return id;

	if (id > G_MAXUINT16) {
		g_set_error (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_INTERNAL,
			     "vendor id overflows");
		return 0x0;
	}

	return (guint16) id;
}

static gboolean
fu_plugin_thunderbolt_is_host (GUdevDevice *device)
{
	g_autoptr(GUdevDevice) parent = NULL;
	const gchar *name;

	/* the (probably safe) assumption this code makes is
	 * that the thunderbolt device which is a direct child
	 * of the domain is the host controller device itself */
	parent = g_udev_device_get_parent (device);
	name = g_udev_device_get_name (parent);
	if (name == NULL)
		return FALSE;

	return g_str_has_prefix (name, "domain");
}

static GFile *
fu_plugin_thunderbolt_find_nvmem (GUdevDevice  *udevice,
				  gboolean      active,
				  GError      **error)
{
	const gchar *nvmem_dir = active ? "nvm_active" : "nvm_non_active";
	const gchar *devpath;
	const gchar *name;
	g_autoptr(GDir) d = NULL;

	devpath = g_udev_device_get_sysfs_path (udevice);
	if (G_UNLIKELY (devpath == NULL)) {
		g_set_error_literal (error,
			     FWUPD_ERROR, FWUPD_ERROR_INTERNAL,
			     "Could not determine sysfs path for device");
		return NULL;
	}

	d = g_dir_open (devpath, 0, error);
	if (d == NULL)
		return NULL;

	while ((name = g_dir_read_name (d)) != NULL) {
		if (g_str_has_prefix (name, nvmem_dir)) {
			g_autoptr(GFile) parent = g_file_new_for_path (devpath);
			g_autoptr(GFile) nvm_dir = g_file_get_child (parent, name);
			return g_file_get_child (nvm_dir, "nvmem");
		}
	}

	g_set_error_literal (error,
			     FWUPD_ERROR, FWUPD_ERROR_NOT_SUPPORTED,
			     "Could not find non-volatile memory location");
	return NULL;
}

static gchar *
fu_plugin_thunderbolt_parse_version (const gchar *version_raw)
{
	g_auto(GStrv) split = NULL;
	if (version_raw == NULL)
		return NULL;
	split = g_strsplit (version_raw, ".", -1);
	if (g_strv_length (split) != 2)
		return NULL;
	return g_strdup_printf ("%02x.%02x",
				(guint) g_ascii_strtoull (split[0], NULL, 16),
				(guint) g_ascii_strtoull (split[1], NULL, 16));
}

static gchar *
fu_plugin_thunderbolt_udev_get_version (GUdevDevice *udevice)
{
	const gchar *version = NULL;

	for (guint i = 0; i < 50; i++) {
		version = g_udev_device_get_sysfs_attr (udevice, "nvm_version");
		if (version != NULL)
			break;
		g_debug ("Attempt %u: Failed to read NVM version", i);
		if (errno != EAGAIN)
			break;
		g_usleep (TBT_NVM_RETRY_TIMEOUT * 1000);
	}

	return fu_plugin_thunderbolt_parse_version (version);
}

static gboolean
fu_plugin_thunderbolt_is_native (GUdevDevice *udevice, gboolean *is_native, GError **error)
{
	g_autoptr(GFile) nvmem = NULL;
	g_autoptr(GBytes) controller_fw = NULL;
	gchar *content;
	gsize length;

	nvmem = fu_plugin_thunderbolt_find_nvmem (udevice, TRUE, error);
	if (nvmem == NULL)
		return FALSE;

	if (!g_file_load_contents (nvmem, NULL, &content, &length, NULL, error))
		return FALSE;

	controller_fw = g_bytes_new_take (content, length);

	return fu_plugin_thunderbolt_controller_is_native (controller_fw,
							   is_native,
							   error);
}

static void
fu_plugin_thunderbolt_add (FuPlugin *plugin, GUdevDevice *device)
{
	FuDevice *dev_tmp;
	const gchar *name;
	const gchar *uuid;
	const gchar *vendor;
	const gchar *devpath;
	const gchar *devtype;
	gboolean is_host;
	gboolean is_safemode = FALSE;
	gboolean is_native = FALSE;
	guint16 did;
	guint16 vid;
	g_autofree gchar *id = NULL;
	g_autofree gchar *version = NULL;
	g_autofree gchar *vendor_id = NULL;
	g_autofree gchar *device_id = NULL;
	g_autoptr(FuDevice) dev = NULL;
	g_autoptr(GError) error_vid = NULL;
	g_autoptr(GError) error_did = NULL;

	uuid = g_udev_device_get_sysfs_attr (device, "unique_id");
	if (uuid == NULL) {
		/* most likely the domain itself, ignore */
		return;
	}

	devpath = g_udev_device_get_sysfs_path (device);

	devtype = g_udev_device_get_devtype (device);
	if (g_strcmp0 (devtype, "thunderbolt_device") != 0) {
		g_debug ("ignoring %s device at %s", devtype, devpath);
		return;
	}

	g_debug ("adding udev device: %s at %s", uuid, devpath);

	id = fu_plugin_thunderbolt_gen_id (device);
	dev_tmp = fu_plugin_cache_lookup (plugin, id);
	if (dev_tmp != NULL) {
		/* devices that are force-powered are re-added */
		g_debug ("ignoring duplicate %s", id);
		return;
	}

	vid = fu_plugin_thunderbolt_udev_get_id (device, "vendor", &error_vid);
	if (vid == 0x0)
		g_warning ("failed to get Vendor ID: %s", error_vid->message);

	did = fu_plugin_thunderbolt_udev_get_id (device, "device", &error_did);
	if (did == 0x0)
		g_warning ("failed to get Device ID: %s", error_did->message);

	dev = fu_device_new ();

	is_host = fu_plugin_thunderbolt_is_host (device);

	version = fu_plugin_thunderbolt_udev_get_version (device);
	/* test for safe mode */
	if (is_host && version == NULL) {
		g_autoptr(GError) error_local = NULL;
		g_autofree gchar *test_safe = NULL;
		g_autofree gchar *safe_path = NULL;
		/* glib can't return a properly mapped -ENODATA but the
		 * kernel only returns -ENODATA or -EAGAIN */
		safe_path = g_build_path ("/", devpath, "nvm_version", NULL);
		if (!g_file_get_contents (safe_path, &test_safe, NULL, &error_local) &&
		    !g_error_matches (error_local, G_IO_ERROR, G_IO_ERROR_WOULD_BLOCK)) {
			g_warning ("%s is in safe mode --  VID/DID will "
				   "need to be set by another plugin",
				   devpath);
			version = g_strdup ("00.00");
			is_safemode = TRUE;
			device_id = g_strdup ("TBT-safemode");
			fu_device_set_metadata_boolean (dev, FU_DEVICE_METADATA_TBT_IS_SAFE_MODE, TRUE);
		}
		fu_plugin_add_report_metadata (plugin, "ThunderboltSafeMode",
					       is_safemode ? "True" : "False");
	}
	if (!is_safemode) {
		if (is_host) {
			g_autoptr(GError) error_local = NULL;
			if (!fu_plugin_thunderbolt_is_native (device, &is_native, &error_local)) {
				g_warning ("failed to get native mode status: %s", error_local->message);
				return;
			}
			fu_plugin_add_report_metadata (plugin,
						       "ThunderboltNative",
						       is_native ? "True" : "False");
		}
		vendor_id = g_strdup_printf ("TBT:0x%04X", (guint) vid);
		device_id = g_strdup_printf ("TBT-%04x%04x%s",
					     (guint) vid,
					     (guint) did,
					     is_native ? "-native" : "");
		fu_device_add_flag (dev, FWUPD_DEVICE_FLAG_UPDATABLE);
	} else {
		fu_device_set_update_error (dev, "Device is in safe mode");
	}

	fu_device_set_platform_id (dev, uuid);

	fu_device_set_metadata (dev, "sysfs-path", devpath);
	name = g_udev_device_get_sysfs_attr (device, "device_name");
	if (name != NULL) {
		if (is_host) {
			g_autofree gchar *pretty_name = NULL;
			pretty_name = g_strdup_printf ("%s Thunderbolt Controller", name);
			fu_device_set_name (dev, pretty_name);
		} else {
			fu_device_set_name (dev, name);
		}
	}
	if (is_host) {
		fu_device_set_summary (dev, "Unmatched performance for high-speed I/O");
		fu_device_add_icon (dev, "computer");
	} else {
		fu_device_add_icon (dev, "audio-card");
	}

	fu_device_set_quirks (dev, fu_plugin_get_quirks (plugin));
	vendor = g_udev_device_get_sysfs_attr (device, "vendor_name");
	if (vendor != NULL)
		fu_device_set_vendor (dev, vendor);
	if (vendor_id != NULL)
		fu_device_set_vendor_id (dev, vendor_id);
	if (device_id != NULL)
		fu_device_add_guid (dev, device_id);
	if (version != NULL)
		fu_device_set_version (dev, version);
	if (is_host)
		fu_device_add_flag (dev, FWUPD_DEVICE_FLAG_INTERNAL);

	fu_plugin_cache_add (plugin, id, dev);
	fu_plugin_device_add (plugin, dev);
}

static void
fu_plugin_thunderbolt_remove (FuPlugin *plugin, GUdevDevice *device)
{
	FuDevice *dev;
	g_autofree gchar *id = NULL;

	id = fu_plugin_thunderbolt_gen_id (device);
	dev = fu_plugin_cache_lookup (plugin, id);
	if (dev == NULL)
		return;

	/* on supported systems other plugins may use a GPIO to force
	 * power on supported devices even when in low power mode --
	 * this will happen in coldplug_prepare and prepare_for_update */
	if (fu_plugin_thunderbolt_is_host (device) &&
	    fu_device_get_metadata_boolean (dev, FU_DEVICE_METADATA_TBT_CAN_FORCE_POWER)) {
		g_debug ("ignoring remove event as force powered");
		return;
	}

	fu_plugin_device_remove (plugin, dev);
	fu_plugin_cache_remove (plugin, id);
}

static void
fu_plugin_thunderbolt_change (FuPlugin *plugin, GUdevDevice *device)
{
	FuDevice *dev;
	g_autofree gchar *version = NULL;
	g_autofree gchar *id = NULL;

	id = fu_plugin_thunderbolt_gen_id (device);
	dev = fu_plugin_cache_lookup (plugin, id);
	if (dev == NULL) {
		g_warning ("got change event for unknown device, adding instead");
		fu_plugin_thunderbolt_add (plugin, device);
		return;
	}

	version = fu_plugin_thunderbolt_udev_get_version (device);
	fu_device_set_version (dev, version);
}

static gboolean
udev_uevent_cb (GUdevClient *udev,
		const gchar *action,
		GUdevDevice *device,
		gpointer     user_data)
{
	FuPlugin *plugin = (FuPlugin *) user_data;
	FuPluginData *data = fu_plugin_get_data (plugin);

	if (action == NULL)
		return TRUE;

	g_debug ("uevent for %s: %s", g_udev_device_get_sysfs_path (device), action);

	if (data->update_notify != NULL) {
		g_debug ("using update notify handler for uevent");

		data->update_notify (plugin, device, action, data->update_data);
		return TRUE;
	}

	if (g_str_equal (action, "add")) {
		fu_plugin_thunderbolt_add (plugin, device);
	} else if (g_str_equal (action, "remove")) {
		fu_plugin_thunderbolt_remove (plugin, device);
	} else if (g_str_equal (action, "change")) {
		fu_plugin_thunderbolt_change (plugin, device);
	}

	return TRUE;
}

static FuPluginValidation
fu_plugin_thunderbolt_validate_firmware (GUdevDevice  *udevice,
					 GBytes       *blob_fw,
					 GError      **error)
{
	g_autoptr(GFile) nvmem = NULL;
	g_autoptr(GBytes) controller_fw = NULL;
	gchar *content;
	gsize length;

	nvmem = fu_plugin_thunderbolt_find_nvmem (udevice, TRUE, error);
	if (nvmem == NULL)
		return VALIDATION_FAILED;

	if (!g_file_load_contents (nvmem, NULL, &content, &length, NULL, error))
		return VALIDATION_FAILED;

	controller_fw = g_bytes_new_take (content, length);

	return fu_plugin_thunderbolt_validate_image (controller_fw,
						     blob_fw,
						     error);
}

static gboolean
fu_plugin_thunderbolt_trigger_update (GUdevDevice  *udevice,
				      GError      **error)
{

	const gchar *devpath;
	ssize_t n;
	int fd;
	int r;
	g_autofree gchar *auth_path = NULL;

	devpath = g_udev_device_get_sysfs_path (udevice);
	auth_path = g_build_filename (devpath, "nvm_authenticate", NULL);

	fd = open (auth_path, O_WRONLY | O_CLOEXEC);
	if (fd < 0) {
		g_set_error (error, G_IO_ERROR,
			     g_io_error_from_errno (errno),
			     "could not open 'nvm_authenticate': %s",
			     g_strerror (errno));
		return FALSE;
	}

	do {
		n = write (fd, "1", 1);
		if (n < 1 && errno != EINTR) {
			g_set_error (error, G_IO_ERROR,
				     g_io_error_from_errno (errno),
				     "could write to 'nvm_authenticate': %s",
				     g_strerror (errno));
			(void) close (fd);
			return FALSE;
		}
	} while (n < 1);

	r = close (fd);
	if (r < 0 && errno != EINTR) {
		g_set_error (error, G_IO_ERROR,
			     g_io_error_from_errno (errno),
			     "could close 'nvm_authenticate': %s",
			     g_strerror (errno));
		return FALSE;
	}

	return TRUE;
}

static gboolean
fu_plugin_thunderbolt_write_firmware (FuDevice     *device,
				      GUdevDevice  *udevice,
				      GBytes       *blob_fw,
				      GError      **error)
{
	gsize fw_size;
	gsize nwritten;
	gssize n;
	g_autoptr(GFile) nvmem = NULL;
	g_autoptr(GOutputStream) os = NULL;

	nvmem = fu_plugin_thunderbolt_find_nvmem (udevice, FALSE, error);
	if (nvmem == NULL)
		return FALSE;

	os = (GOutputStream *) g_file_append_to (nvmem,
						 G_FILE_CREATE_NONE,
						 NULL,
						 error);

	if (os == NULL)
		return FALSE;

	nwritten = 0;
	fw_size = g_bytes_get_size (blob_fw);
	fu_device_set_progress_full (device, nwritten, fw_size);

	do {
		g_autoptr(GBytes) fw_data = NULL;

		fw_data = g_bytes_new_from_bytes (blob_fw,
						  nwritten,
						  fw_size - nwritten);

		n = g_output_stream_write_bytes (os,
						 fw_data,
						 NULL,
						 error);
		if (n < 0)
			return FALSE;

		nwritten += n;
		fu_device_set_progress_full (device, nwritten, fw_size);

	} while (nwritten < fw_size);

	if (nwritten != fw_size) {
		g_set_error_literal (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_WRITE,
				     "Could not write all data to nvmem");
		return FALSE;
	}

	return g_output_stream_close (os, NULL, error);
}

typedef struct UpdateData {

	gboolean   have_device;
	GMainLoop *mainloop;
	const gchar *target_uuid;
	guint      timeout_id;

	GHashTable *changes;
} UpdateData;

static gboolean
on_wait_for_device_timeout (gpointer user_data)
{
	UpdateData *data = (UpdateData *) user_data;
	g_main_loop_quit (data->mainloop);
	data->timeout_id = 0;
	return FALSE;
}

static void
on_wait_for_device_added (FuPlugin    *plugin,
			  GUdevDevice *device,
			  UpdateData  *up_data)
{
	FuDevice  *dev;
	const gchar *uuid;
	const gchar *path;
	g_autofree gchar *version = NULL;
	g_autofree gchar *id = NULL;

	uuid = g_udev_device_get_sysfs_attr (device, "unique_id");
	if (uuid == NULL)
		return;

	dev = g_hash_table_lookup (up_data->changes, uuid);
	if (dev == NULL) {
		/* a previously unknown device, add it via
		 * the normal way */
		fu_plugin_thunderbolt_add (plugin, device);
		return;
	}

	/* ensure the device path is correct */
	path = g_udev_device_get_sysfs_path (device);
	fu_device_set_metadata (dev, "sysfs-path", path);

	/* make sure the version is correct, might have changed
	 * after update. */
	version = fu_plugin_thunderbolt_udev_get_version (device);
	fu_device_set_version (dev, version);

	id = fu_plugin_thunderbolt_gen_id (device);
	fu_plugin_cache_add (plugin, id, dev);

	g_hash_table_remove (up_data->changes, uuid);

	/* check if this device is the target*/
	if (g_str_equal (uuid, up_data->target_uuid)) {
		up_data->have_device = TRUE;
		g_debug ("target (%s) re-appeared", uuid);
		g_main_loop_quit (up_data->mainloop);
	}
}

static void
on_wait_for_device_removed (FuPlugin    *plugin,
			    GUdevDevice *device,
			    UpdateData *up_data)
{
	g_autofree gchar *id = NULL;
	FuDevice  *dev;
	const gchar *uuid;

	id = fu_plugin_thunderbolt_gen_id (device);
	dev = fu_plugin_cache_lookup (plugin, id);

	if (dev == NULL)
		return;

	fu_plugin_cache_remove (plugin, id);
	uuid = fu_device_get_platform_id (dev);
	g_hash_table_insert (up_data->changes,
			     (gpointer) uuid,
			     g_object_ref (dev));

	/* check if this device is the target */
	if (g_str_equal (uuid, up_data->target_uuid)) {
		up_data->have_device = FALSE;
		g_debug ("target (%s) disappeared", uuid);
	}
}

static void
on_wait_for_device_notify (FuPlugin    *plugin,
			   GUdevDevice *device,
			   const char  *action,
			   gpointer    user_data)
{
	UpdateData *up_data = (UpdateData *) user_data;

	/* nb: action cannot be NULL since we are only called from
	 *     udev_event_cb, which ensures that */
	if (g_str_equal (action, "add")) {
		on_wait_for_device_added (plugin, device, up_data);
	} else if (g_str_equal (action, "remove")) {
		on_wait_for_device_removed (plugin, device, up_data);
	} else if (g_str_equal (action, "change")) {
		fu_plugin_thunderbolt_change (plugin, device);
	}
}

static void
remove_leftover_devices (gpointer key,
			 gpointer value,
			 gpointer user_data)
{
	FuPlugin *plugin = FU_PLUGIN (user_data);
	FuDevice *dev = FU_DEVICE (value);
	const gchar *syspath = fu_device_get_metadata (dev, "sysfs-path");
	g_autofree gchar *id = NULL;

	id = fu_plugin_thunderbolt_gen_id_from_syspath (syspath);

	fu_plugin_cache_remove (plugin, id);
	fu_plugin_device_remove (plugin, dev);
}

static gboolean
fu_plugin_thunderbolt_wait_for_device (FuPlugin  *plugin,
				       FuDevice  *dev,
				       GError   **error)
{
	FuPluginData *data = fu_plugin_get_data (plugin);
	UpdateData up_data = { TRUE, };
	g_autoptr(GMainLoop) mainloop = NULL;
	g_autoptr(GHashTable) changes = NULL;

	up_data.mainloop = mainloop = g_main_loop_new (NULL, FALSE);
	up_data.target_uuid = fu_device_get_platform_id (dev);

	/* this will limit the maximum amount of time we wait for
	 * the device (i.e. 'dev') to re-appear. */
	up_data.timeout_id = g_timeout_add (data->timeout,
					    on_wait_for_device_timeout,
					    &up_data);

	/* this will capture the device added, removed, changed
	 * signals while we are updating.  */
	data->update_data = &up_data;
	data->update_notify = on_wait_for_device_notify;

	changes = g_hash_table_new_full (g_str_hash, g_str_equal, NULL, g_object_unref);
	up_data.changes = changes;

	/* now we wait ... */
	g_main_loop_run (mainloop);

	/* restore original udev change handler */
	data->update_data = NULL;
	data->update_notify = NULL;

	if (up_data.timeout_id > 0)
		g_source_remove (up_data.timeout_id);

	if (!up_data.have_device) {
		g_set_error_literal (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_INTERNAL,
				     "timed out while waiting for device");
		return FALSE;
	}

	g_hash_table_foreach (changes, remove_leftover_devices, plugin);

	return TRUE;
}

/* internal interface  */

void
fu_plugin_thunderbolt_set_timeout (FuPlugin *plugin, guint timeout_ms)
{
	FuPluginData *data = fu_plugin_get_data (plugin);
	data->timeout = timeout_ms;
}

/* virtual functions */

void
fu_plugin_init (FuPlugin *plugin)
{
	FuPluginData *data = fu_plugin_alloc_data (plugin, sizeof (FuPluginData));
	const gchar *subsystems[] = { "thunderbolt", NULL };

	data->udev = g_udev_client_new (subsystems);
	g_signal_connect (data->udev, "uevent",
			  G_CALLBACK (udev_uevent_cb), plugin);

	data->timeout = FU_PLUGIN_THUNDERBOLT_UPDATE_TIMEOUT_MS;
}

void
fu_plugin_destroy (FuPlugin *plugin)
{
	FuPluginData *data = fu_plugin_get_data (plugin);
	g_object_unref (data->udev);
}

static gboolean
fu_plugin_thunderbolt_coldplug (FuPlugin *plugin, GError **error)
{
	FuPluginData *data = fu_plugin_get_data (plugin);
	GList *devices;

	devices = g_udev_client_query_by_subsystem (data->udev, "thunderbolt");
	for (GList *l = devices; l != NULL; l = l->next) {
		GUdevDevice *device = l->data;
		fu_plugin_thunderbolt_add (plugin, device);
	}

	g_list_foreach (devices, (GFunc) g_object_unref, NULL);
	g_list_free (devices);

	return TRUE;
}

gboolean
fu_plugin_coldplug (FuPlugin *plugin, GError **error)
{
	return fu_plugin_thunderbolt_coldplug (plugin, error);
}

gboolean
fu_plugin_recoldplug (FuPlugin *plugin, GError **error)
{
	return fu_plugin_thunderbolt_coldplug (plugin, error);
}

gboolean
fu_plugin_update (FuPlugin *plugin,
		  FuDevice *dev,
		  GBytes *blob_fw,
		  FwupdInstallFlags flags,
		  GError **error)
{
	FuPluginData *data = fu_plugin_get_data (plugin);
	const gchar *devpath;
	guint64 status;
	g_autoptr(GUdevDevice) udevice = NULL;
	g_autoptr(GError) error_local = NULL;
	gboolean force = (flags & FWUPD_INSTALL_FLAG_FORCE) != 0;
	FuPluginValidation validation;

	devpath = fu_device_get_metadata (dev, "sysfs-path");
	g_return_val_if_fail (devpath, FALSE);

	udevice = g_udev_client_query_by_sysfs_path (data->udev, devpath);
	if (udevice == NULL) {
		g_set_error (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_NOT_FOUND,
			     "could not find thunderbolt device at %s",
			     devpath);
		return FALSE;
	}

	validation = fu_plugin_thunderbolt_validate_firmware (udevice,
							      blob_fw,
							      &error_local);
	if (validation != VALIDATION_PASSED) {
		g_autofree gchar* msg = NULL;
		switch (validation) {
		case VALIDATION_FAILED:
			msg = g_strdup_printf ("could not validate firmware: %s",
					       error_local->message);
			break;
		case UNKNOWN_DEVICE:
			msg = g_strdup ("firmware validation seems to be passed but the device is unknown");
			break;
		default:
			break;
		}
		if (!force) {
			g_set_error (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_INVALID_FILE,
				     "%s. "
				     "See https://github.com/hughsie/fwupd/wiki/Thunderbolt:-Validation-failed-or-unknown-device for more information.",
				     msg);
			return FALSE;
		}
		g_warning ("%s", msg);
	}

	fu_device_set_status (dev, FWUPD_STATUS_DEVICE_WRITE);
	if (!fu_plugin_thunderbolt_write_firmware (dev, udevice, blob_fw, &error_local)) {
		g_set_error (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_WRITE,
			     "could not write firmware to thunderbolt device at %s: %s",
			     devpath, error_local->message);
		return FALSE;
	}

	if (!fu_plugin_thunderbolt_trigger_update (udevice, &error_local)) {
		g_set_error (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_NOT_SUPPORTED,
			     "Could not start thunderbolt device upgrade: %s",
			     error_local->message);
		return FALSE;
	}

	fu_device_set_status (dev, FWUPD_STATUS_DEVICE_RESTART);

	/* the device will disappear and we need to wait until it reappears,
	 * and then check if we find an error */
	if (!fu_plugin_thunderbolt_wait_for_device (plugin, dev, &error_local)) {
		g_set_error (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_NOT_FOUND,
			     "could not detect device after update: %s",
			     error_local->message);
		return FALSE;
	}

	/* now check if the update actually worked */
	status = udev_device_get_sysattr_guint64 (udevice,
						  "nvm_authenticate",
						  &error_local);

	/* anything else then 0x0 means we got an error */
	if (status != 0x0) {
		g_set_error (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_INTERNAL,
			     "update failed (status %" G_GINT64_MODIFIER "x)", status);
		return FALSE;
	}

	return TRUE;
}
