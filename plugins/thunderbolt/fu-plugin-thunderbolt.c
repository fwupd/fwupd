/*
 * Copyright (C) 2017 Christian J. Kellner <christian@kellner.me>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include <sys/types.h>
#include <sys/stat.h>
#include <sys/utsname.h>
#include <fcntl.h>
#include <errno.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>

#include "fu-plugin-vfuncs.h"
#include "fu-hash.h"
#include "fu-device-metadata.h"
#include "fu-thunderbolt-image.h"

#define TBT_NVM_RETRY_TIMEOUT				200	/* ms */
#define FU_PLUGIN_THUNDERBOLT_UPDATE_TIMEOUT		60000	/* ms */

typedef void (*UEventNotify) (FuPlugin	  *plugin,
			      GUdevDevice *udevice,
			      const gchar *action,
			      gpointer     user_data);

struct FuPluginData {
	GUdevClient   *udev;
};

static gboolean
fu_plugin_thunderbolt_safe_kernel (FuPlugin *plugin, GError **error)
{
	g_autofree gchar *minimum_kernel = NULL;
	struct utsname name_tmp;

	memset (&name_tmp, 0, sizeof(struct utsname));
	if (uname (&name_tmp) < 0) {
		g_debug ("Failed to read current kernel version");
		return TRUE;
	}

	minimum_kernel = fu_plugin_get_config_value (plugin, "MinimumKernelVersion");
	if (minimum_kernel == NULL) {
		g_debug ("Ignoring kernel safety checks");
		return TRUE;
	}

	if (fu_common_vercmp_full (name_tmp.release,
				   minimum_kernel,
				   FWUPD_VERSION_FORMAT_TRIPLET) < 0) {
		g_set_error (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_INTERNAL,
			     "kernel %s may not have full Thunderbolt support",
			     name_tmp.release);
		return FALSE;
	}
	g_debug ("Using kernel %s (minimum %s)", name_tmp.release, minimum_kernel);

	return TRUE;
}

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

static gboolean
udev_device_get_sysattr_guint64 (GUdevDevice *device,
				 const gchar *name,
				 guint64 *val_out,
				 GError **error)
{
	const gchar *sysfs;

	sysfs = g_udev_device_get_sysfs_attr (device, name);
	if (sysfs == NULL) {
		g_set_error (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_INTERNAL,
			     "missing sysfs attribute %s", name);
		return FALSE;
	}

	*val_out = g_ascii_strtoull (sysfs, NULL, 16);
	if (*val_out == 0x0) {
		g_set_error (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_INTERNAL,
			     "failed to parse %s", sysfs);
		return FALSE;
	}

	return TRUE;
}

static guint16
fu_plugin_thunderbolt_udev_get_uint16 (GUdevDevice *device,
				       const gchar *name,
				       GError **error)
{

	guint64 id = 0;

	if (!udev_device_get_sysattr_guint64 (device, name, &id, error))
		return 0x0;

	if (id > G_MAXUINT16) {
		g_set_error (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_INTERNAL,
			     "%s overflows",
			     name);
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
	gsize nr_chunks;
	g_autoptr(GFile) nvmem = NULL;
	g_autoptr(GBytes) controller_fw = NULL;
	g_autoptr(GInputStream) istr = NULL;

	nvmem = fu_plugin_thunderbolt_find_nvmem (udevice, TRUE, error);
	if (nvmem == NULL)
		return FALSE;

	/* read just enough bytes to read the status byte */
	nr_chunks = (FU_TBT_OFFSET_NATIVE + FU_TBT_CHUNK_SZ - 1) / FU_TBT_CHUNK_SZ;
	istr = G_INPUT_STREAM (g_file_read (nvmem, NULL, error));
	if (istr == NULL)
		return FALSE;
	controller_fw = g_input_stream_read_bytes (istr,
						   nr_chunks * FU_TBT_CHUNK_SZ,
						   NULL, error);
	if (controller_fw == NULL)
		return FALSE;

	return fu_thunderbolt_image_controller_is_native (controller_fw,
							  is_native,
							  error);
}

static gboolean
fu_plugin_thunderbolt_can_update (GUdevDevice *udevice)
{
	g_autoptr(GError) nvmem_error = NULL;
	g_autoptr(GFile) non_active_nvmem = NULL;

	non_active_nvmem = fu_plugin_thunderbolt_find_nvmem (udevice, FALSE,
							     &nvmem_error);
	if (non_active_nvmem == NULL) {
		g_debug ("%s", nvmem_error->message);
		return FALSE;
	}

	return TRUE;
}

static void
fu_plugin_thunderbolt_add (FuPlugin *plugin, GUdevDevice *device)
{
	FuDevice *dev_tmp;
	const gchar *name = NULL;
	const gchar *uuid;
	const gchar *vendor;
	const gchar *devpath;
	const gchar *devtype;
	gboolean is_host;
	gboolean is_safemode = FALSE;
	gboolean is_native = FALSE;
	guint16 did;
	guint16 vid;
	guint16 gen;
	g_autofree gchar *id = NULL;
	g_autofree gchar *version = NULL;
	g_autofree gchar *vendor_id = NULL;
	g_autofree gchar *device_id = NULL;
	g_autofree gchar *device_id_with_path = NULL;
	g_autoptr(FuDevice) dev = NULL;
	g_autoptr(GError) error_vid = NULL;
	g_autoptr(GError) error_did = NULL;
	g_autoptr(GError) error_gen = NULL;
	g_autoptr(GError) error_setup = NULL;

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

	/* these may be missing on ICL or later */
	vid = fu_plugin_thunderbolt_udev_get_uint16 (device, "vendor", &error_vid);
	if (vid == 0x0)
		g_debug ("failed to get Vendor ID: %s", error_vid->message);

	did = fu_plugin_thunderbolt_udev_get_uint16 (device, "device", &error_did);
	if (did == 0x0)
		g_debug ("failed to get Device ID: %s", error_did->message);

	/* requires kernel 5.5 or later, non-fatal if not available */
	gen = fu_plugin_thunderbolt_udev_get_uint16 (device, "generation", &error_gen);
	if (gen == 0)
		g_debug ("Unable to read generation: %s", error_gen->message);

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
		if (fu_plugin_thunderbolt_can_update (device)) {
			/* USB4 controllers don't have a concept of legacy vs native
			 * so don't try to read a native attribute from their NVM */
			if (is_host && gen < 4) {
				g_autoptr(GError) native_error = NULL;
				g_autoptr(GUdevDevice) udev_parent = NULL;
				if (!fu_plugin_thunderbolt_is_native (device,
								      &is_native,
								      &native_error)) {
					g_warning ("failed to get native mode status: %s",
						   native_error->message);
					return;
				}
				fu_plugin_add_report_metadata (plugin,
							       "ThunderboltNative",
							       is_native ? "True" : "False");
				udev_parent = g_udev_device_get_parent_with_subsystem (device, "pci", NULL);
				if (udev_parent != NULL)
					device_id_with_path = g_strdup_printf ("TBT-%04x%04x%s-%s",
									       (guint) vid,
									       (guint) did,
									       is_native ? "-native" : "",
									       g_udev_device_get_property (udev_parent,
													   "PCI_SLOT_NAME"));
			}
			vendor_id = g_strdup_printf ("TBT:0x%04X", (guint) vid);
			device_id = g_strdup_printf ("TBT-%04x%04x%s",
						     (guint) vid,
						     (guint) did,
						     is_native ? "-native" : "");
			fu_device_add_flag (dev, FWUPD_DEVICE_FLAG_UPDATABLE);
			fu_device_add_flag (dev, FWUPD_DEVICE_FLAG_DUAL_IMAGE);
		} else {
			device_id = g_strdup ("TBT-fixed");
			fu_device_set_update_error (dev, "Missing non-active nvmem");
		}
	} else {
		fu_device_set_update_error (dev, "Device is in safe mode");
	}

	fu_device_set_physical_id (dev, uuid);

	fu_device_set_metadata (dev, "sysfs-path", devpath);
	if (!is_host)
		name = g_udev_device_get_sysfs_attr (device, "device_name");
	if (name == NULL) {
		if (gen == 4)
			name = "USB4 Controller";
		else
			name = "Thunderbolt Controller";
	}
	fu_device_set_name (dev, name);
	if (is_host)
		fu_device_set_summary (dev, "Unmatched performance for high-speed I/O");
	fu_device_add_icon (dev, "thunderbolt");
	fu_device_set_protocol (dev, "com.intel.thunderbolt");

	fu_device_set_quirks (dev, fu_plugin_get_quirks (plugin));
	vendor = g_udev_device_get_sysfs_attr (device, "vendor_name");
	if (vendor != NULL)
		fu_device_set_vendor (dev, vendor);
	if (vendor_id != NULL)
		fu_device_set_vendor_id (dev, vendor_id);
	if (device_id != NULL)
		fu_device_add_instance_id (dev, device_id);
	if (device_id_with_path != NULL)
		fu_device_add_instance_id (dev, device_id_with_path);
	if (version != NULL)
		fu_device_set_version (dev, version, FWUPD_VERSION_FORMAT_PAIR);
	if (is_host)
		fu_device_add_flag (dev, FWUPD_DEVICE_FLAG_INTERNAL);
	fu_device_add_flag (dev, FWUPD_DEVICE_FLAG_REQUIRE_AC);

	/* we never open the device, so convert the instance IDs */
	if (!fu_device_setup (dev, &error_setup)) {
		g_warning ("failed to setup: %s", error_setup->message);
		return;
	}
	fu_plugin_cache_add (plugin, id, dev);
	fu_plugin_device_add (plugin, dev);

	/* inhibit the idle sleep of the daemon */
	fu_plugin_add_rule (plugin, FU_PLUGIN_RULE_INHIBITS_IDLE,
			    "thunderbolt requires device wakeup");
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
	    !fu_device_has_flag (dev, FWUPD_DEVICE_FLAG_WAIT_FOR_REPLUG) &&
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
	fu_device_set_version (dev, version, FWUPD_VERSION_FORMAT_PAIR);
}

static gboolean
udev_uevent_cb (GUdevClient *udev,
		const gchar *action,
		GUdevDevice *device,
		gpointer     user_data)
{
	FuPlugin *plugin = (FuPlugin *) user_data;

	if (action == NULL)
		return TRUE;

	g_debug ("uevent for %s: %s", g_udev_device_get_sysfs_path (device), action);

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
	return fu_thunderbolt_image_validate (controller_fw, blob_fw, error);
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
				     "could not write to 'nvm_authenticate': %s",
				     g_strerror (errno));
			(void) close (fd);
			return FALSE;
		}
	} while (n < 1);

	r = close (fd);
	if (r < 0 && errno != EINTR) {
		g_set_error (error, G_IO_ERROR,
			     g_io_error_from_errno (errno),
			     "could not close 'nvm_authenticate': %s",
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

/* virtual functions */

void
fu_plugin_init (FuPlugin *plugin)
{
	FuPluginData *data = fu_plugin_alloc_data (plugin, sizeof (FuPluginData));
	const gchar *subsystems[] = { "thunderbolt", NULL };

	fu_plugin_set_build_hash (plugin, FU_BUILD_HASH);
	data->udev = g_udev_client_new (subsystems);
	g_signal_connect (data->udev, "uevent",
			  G_CALLBACK (udev_uevent_cb), plugin);

	/* dell-dock plugin uses a slower bus for flashing */
	fu_plugin_add_rule (plugin, FU_PLUGIN_RULE_BETTER_THAN, "dell_dock");
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
fu_plugin_startup (FuPlugin *plugin, GError **error)
{
	return fu_plugin_thunderbolt_safe_kernel (plugin, error);
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
	g_autoptr(GUdevDevice) udevice = NULL;
	g_autoptr(GError) error_local = NULL;
	gboolean install_force = (flags & FWUPD_INSTALL_FLAG_FORCE) != 0;
	gboolean device_ignore_validation = fu_device_has_flag (dev, FWUPD_DEVICE_FLAG_IGNORE_VALIDATION);
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
		if (!install_force && !device_ignore_validation) {
			g_set_error (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_INVALID_FILE,
				     "%s. "
				     "See https://github.com/fwupd/fwupd/wiki/Thunderbolt:-Validation-failed-or-unknown-device for more information.",
				     msg);
			return FALSE;
		}
		g_warning ("%s", msg);
	}

	fu_device_set_status (dev, FWUPD_STATUS_DEVICE_WRITE);
	if (!fu_plugin_thunderbolt_write_firmware (dev, udevice, blob_fw, error)) {
		g_prefix_error (error,
				"could not write firmware to thunderbolt device at %s: ",
				devpath);
		return FALSE;
	}

	if (!fu_plugin_thunderbolt_trigger_update (udevice, error)) {
		g_prefix_error (error, "could not start thunderbolt device upgrade: ");
		return FALSE;
	}

	fu_device_set_status (dev, FWUPD_STATUS_DEVICE_RESTART);
	fu_device_set_remove_delay (dev, FU_PLUGIN_THUNDERBOLT_UPDATE_TIMEOUT);
	fu_device_add_flag (dev, FWUPD_DEVICE_FLAG_WAIT_FOR_REPLUG);

	return TRUE;
}

gboolean
fu_plugin_update_attach (FuPlugin *plugin,
			 FuDevice *dev,
			 GError **error)
{
	FuPluginData *data = fu_plugin_get_data (plugin);
	const gchar *devpath;
	const gchar *attribute;
	guint64 status;
	g_autoptr(GUdevDevice) udevice = NULL;

	devpath = fu_device_get_metadata (dev, "sysfs-path");
	udevice = g_udev_client_query_by_sysfs_path (data->udev, devpath);
	if (udevice == NULL) {
		g_set_error (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_NOT_FOUND,
			     "could not find thunderbolt device at %s",
			     devpath);
		return FALSE;
	}

	/* now check if the update actually worked */
	attribute = g_udev_device_get_sysfs_attr (udevice, "nvm_authenticate");
	if (attribute == NULL) {
		g_set_error (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_INTERNAL,
			     "failed to find nvm_authenticate attribute for %s",
			     fu_device_get_name (dev));
		return FALSE;
	}
	status = g_ascii_strtoull (attribute, NULL, 16);
	if (status == G_MAXUINT64 && errno == ERANGE) {
		g_set_error (error, G_IO_ERROR,
			     g_io_error_from_errno (errno),
			     "failed to read 'nvm_authenticate: %s",
			     g_strerror (errno));
		return FALSE;
	}

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
