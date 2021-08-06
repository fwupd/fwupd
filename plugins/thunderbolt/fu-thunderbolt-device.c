/*
 * Copyright (C) 2017 Christian J. Kellner <christian@kellner.me>
 * Copyright (C) 2020 Mario Limonciello <mario.limonciello@dell.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include <fwupdplugin.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <errno.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>

#include "fu-thunderbolt-device.h"
#include "fu-thunderbolt-firmware.h"
#include "fu-thunderbolt-firmware-update.h"

typedef enum {
	FU_THUNDERBOLT_DEVICE_TYPE_DEVICE_CONTROLLER,
	FU_THUNDERBOLT_DEVICE_TYPE_HOST_CONTROLLER,
	FU_THUNDERBOLT_DEVICE_TYPE_RETIMER
} FuThunderboltDeviceType;

struct _FuThunderboltDevice {
	FuUdevDevice		 parent_instance;
	FuThunderboltDeviceType	 device_type;
	gboolean		 safe_mode;
	gboolean		 is_native;
	guint16			 gen;
	gchar			*devpath;
	const gchar		*auth_method;
};

#define TBT_NVM_RETRY_TIMEOUT				200	/* ms */
#define FU_PLUGIN_THUNDERBOLT_UPDATE_TIMEOUT		60000	/* ms */

G_DEFINE_TYPE (FuThunderboltDevice, fu_thunderbolt_device, FU_TYPE_UDEV_DEVICE)

static GFile *
fu_thunderbolt_device_find_nvmem (FuThunderboltDevice	*self,
				  gboolean		 active,
				  GError		**error)
{

	const gchar *nvmem_dir = active ? "nvm_active" : "nvm_non_active";
	const gchar *name;
	g_autoptr(GDir) d = NULL;

	if (G_UNLIKELY (self->devpath == NULL)) {
		g_set_error_literal (error,
			     FWUPD_ERROR, FWUPD_ERROR_INTERNAL,
			     "Could not determine sysfs path for device");
		return NULL;
	}

	d = g_dir_open (self->devpath, 0, error);
	if (d == NULL)
		return NULL;

	while ((name = g_dir_read_name (d)) != NULL) {
		if (g_str_has_prefix (name, nvmem_dir)) {
			g_autoptr(GFile) parent = g_file_new_for_path (self->devpath);
			g_autoptr(GFile) nvm_dir = g_file_get_child (parent, name);
			return g_file_get_child (nvm_dir, "nvmem");
		}
	}

	g_set_error_literal (error,
			     FWUPD_ERROR, FWUPD_ERROR_NOT_SUPPORTED,
			     "Could not find non-volatile memory location");
	return NULL;
}

static gboolean
fu_thunderbolt_device_read_status_block (FuThunderboltDevice *self, GError **error)
{
	gsize nr_chunks;
	g_autoptr(GFile) nvmem = NULL;
	g_autoptr(GBytes) controller_fw = NULL;
	g_autoptr(GInputStream) istr = NULL;
	g_autoptr(FuThunderboltFirmware) firmware = fu_thunderbolt_firmware_new ();

	nvmem = fu_thunderbolt_device_find_nvmem (self, TRUE, error);
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
	if (!fu_firmware_parse (FU_FIRMWARE (firmware),
				controller_fw,
				FWUPD_INSTALL_FLAG_NONE,
				error))
		return FALSE;
	self->is_native = fu_thunderbolt_firmware_is_native (firmware);
	return TRUE;
}

static gboolean
fu_thunderbolt_device_check_authorized (FuThunderboltDevice *self, GError **error)
{
	guint64 status;
	g_autofree gchar *attribute = NULL;
	const gchar *update_error = NULL;
	/* read directly from file to prevent udev caching */
	g_autofree gchar *safe_path = g_build_path ("/", self->devpath, "authorized", NULL);

	if (!g_file_test (safe_path, G_FILE_TEST_EXISTS)) {
		g_set_error_literal (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_NOT_SUPPORTED,
				     "missing authorized attribute");
		return FALSE;
	}

	if (!g_file_get_contents (safe_path, &attribute, NULL, error))
		return FALSE;
	status = g_ascii_strtoull (attribute, NULL, 16);
	if (status == G_MAXUINT64 && errno == ERANGE) {
		g_set_error (error, G_IO_ERROR,
			     g_io_error_from_errno (errno),
			     "failed to read 'authorized: %s",
			     g_strerror (errno));
		return FALSE;
	}
	if (status == 1 || status == 2)
		fu_device_add_flag (FU_DEVICE (self), FWUPD_DEVICE_FLAG_UPDATABLE);
	else
		update_error = "Not authorized";
	fu_device_set_update_error (FU_DEVICE (self), update_error);

	return TRUE;
}

static gboolean
fu_thunderbolt_device_can_update (FuThunderboltDevice *self)
{
	g_autoptr(GError) nvmem_error = NULL;
	g_autoptr(GFile) non_active_nvmem = NULL;

	non_active_nvmem = fu_thunderbolt_device_find_nvmem (self, FALSE,
							     &nvmem_error);
	if (non_active_nvmem == NULL) {
		g_debug ("%s", nvmem_error->message);
		return FALSE;
	}

	return TRUE;
}

static gboolean
fu_thunderbolt_device_get_version (FuThunderboltDevice *self, GError **error)
{
	g_auto(GStrv) split = NULL;
	g_autofree gchar *version_raw = NULL;
	g_autofree gchar *version = NULL;
	/* read directly from file to prevent udev caching */
	g_autofree gchar *safe_path = g_build_path ("/", self->devpath, "nvm_version", NULL);

	if (!g_file_test (safe_path, G_FILE_TEST_EXISTS)) {
		g_set_error_literal (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_NOT_SUPPORTED,
				     "missing nvm_version attribute");
		return FALSE;
	}

	for (guint i = 0; i < 50; i++) {
		g_autoptr(GError) error_local = NULL;
		/* glib can't return a properly mapped -ENODATA but the
		 * kernel only returns -ENODATA or -EAGAIN */
		if (g_file_get_contents (safe_path, &version_raw, NULL, &error_local))
			break;
		g_debug ("Attempt %u: Failed to read NVM version", i);
		g_usleep (TBT_NVM_RETRY_TIMEOUT * 1000);
		/* safe mode probably */
		if (g_error_matches (error_local, G_IO_ERROR, G_IO_ERROR_WOULD_BLOCK))
			break;
	}

	if (version_raw == NULL) {
		g_set_error_literal (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_INTERNAL,
				     "failed to read NVM");
		return FALSE;
	}
	split = g_strsplit (version_raw, ".", -1);
	if (g_strv_length (split) != 2) {
		g_set_error (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_NOT_SUPPORTED,
			     "invalid nvm_version format: %s", version_raw);
		return FALSE;
	}

	version = g_strdup_printf ("%02x.%02x",
				   (guint) g_ascii_strtoull (split[0], NULL, 16),
				   (guint) g_ascii_strtoull (split[1], NULL, 16));
	fu_device_set_version (FU_DEVICE (self), version);
	return TRUE;
}

static void
fu_thunderbolt_device_check_safe_mode (FuThunderboltDevice *self)
{
	/* failed to read, for host check for safe mode */
	if (self->device_type != FU_THUNDERBOLT_DEVICE_TYPE_DEVICE_CONTROLLER)
		return;
	g_warning ("%s is in safe mode --  VID/DID will "
		   "need to be set by another plugin",
		self->devpath);
	self->safe_mode = TRUE;
	fu_device_set_version (FU_DEVICE (self), "00.00");
	fu_device_add_instance_id (FU_DEVICE (self), "TBT-safemode");
	fu_device_set_metadata_boolean (FU_DEVICE (self), FU_DEVICE_METADATA_TBT_IS_SAFE_MODE, TRUE);
}

static const gchar*
fu_thunderbolt_device_type_to_string (FuThunderboltDevice *self)
{
	if (self->device_type == FU_THUNDERBOLT_DEVICE_TYPE_HOST_CONTROLLER) {
		if (self->gen >= 4)
			return "USB4 host controller";
		else
			return "Thunderbolt host controller";
	}
	if (self->device_type == FU_THUNDERBOLT_DEVICE_TYPE_DEVICE_CONTROLLER) {
		if (self->gen >= 4)
			return "USB4 device controller";
		else
			return "Thunderbolt device controller";
	}
	if (self->device_type == FU_THUNDERBOLT_DEVICE_TYPE_RETIMER)
		return "USB4 Retimer";
	return "Unknown";
}

static void
fu_thunderbolt_device_to_string (FuDevice *device, guint idt, GString *str)
{
	FuThunderboltDevice *self = FU_THUNDERBOLT_DEVICE (device);

	/* FuUdevDevice->to_string */
	FU_DEVICE_CLASS (fu_thunderbolt_device_parent_class)->to_string (device, idt, str);

	fu_common_string_append_kv (str, idt, "Device Type", fu_thunderbolt_device_type_to_string (self));
	fu_common_string_append_kb (str, idt, "Safe Mode", self->safe_mode);
	fu_common_string_append_kb (str, idt, "Native mode", self->is_native);
	fu_common_string_append_ku (str, idt, "Generation", self->gen);
	fu_common_string_append_kv (str, idt, "AuthAttribute", self->auth_method);
}

static gboolean
fu_thunderbolt_device_probe (FuDevice *device, GError **error)
{
	FuThunderboltDevice *self = FU_THUNDERBOLT_DEVICE (device);
	const gchar *tmp = fu_udev_device_get_devtype (FU_UDEV_DEVICE (device));

	/* FuUdevDevice->probe */
	if (!FU_DEVICE_CLASS (fu_thunderbolt_device_parent_class)->probe (device, error))
		return FALSE;

	/* device */
	if (g_strcmp0 (tmp, "thunderbolt_device") == 0) {
		tmp = fu_udev_device_get_sysfs_attr (FU_UDEV_DEVICE (device), "unique_id", NULL);
		if (tmp != NULL)
			fu_device_set_physical_id (device, tmp);
	/* retimer */
	} else if (g_strcmp0 (tmp, "thunderbolt_retimer") == 0) {
		self->device_type = FU_THUNDERBOLT_DEVICE_TYPE_RETIMER;
		tmp = g_path_get_basename (fu_udev_device_get_sysfs_path (FU_UDEV_DEVICE (device)));
		if (tmp != NULL)
			fu_device_set_physical_id (device, tmp);
	/* domain or unsupported */
	} else {
		g_set_error (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_NOT_SUPPORTED,
			     "%s not used", tmp);
		return FALSE;
	}

	return TRUE;
}

static guint16
fu_thunderbolt_device_get_attr_uint16 (FuThunderboltDevice *self,
				       const gchar *name,
				       GError **error)
{
	const gchar *str;
	guint64 val;

	str = fu_udev_device_get_sysfs_attr (FU_UDEV_DEVICE (self), name, error);
	if (str == NULL)
		return 0x0;

	val = g_ascii_strtoull (str, NULL, 16);
	if (val == 0x0) {
		g_set_error (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_INTERNAL,
			     "failed to parse %s", str);
		return 0;
	}
	if (val > G_MAXUINT16) {
		g_set_error (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_INTERNAL,
			     "%s overflows",
			     name);
		return 0x0;
	}
	return (guint16) val;
}

static gboolean
fu_thunderbolt_device_setup_controller (FuDevice *device, GError **error)
{
	FuThunderboltDevice *self = FU_THUNDERBOLT_DEVICE (device);
	const gchar *tmp = NULL;
	guint16 did;
	guint16 vid;
	g_autoptr(GError) error_gen = NULL;
	g_autofree gchar *parent_name = fu_udev_device_get_parent_name (FU_UDEV_DEVICE (self));

	/* these may be missing on ICL or later */
	vid = fu_udev_device_get_vendor (FU_UDEV_DEVICE (self));
	if (vid == 0x0)
		g_debug ("failed to get Vendor ID");

	did = fu_udev_device_get_model (FU_UDEV_DEVICE (self));
	if (did == 0x0)
		g_debug ("failed to get Device ID");

	/* requires kernel 5.5 or later, non-fatal if not available */
	self->gen = fu_thunderbolt_device_get_attr_uint16 (self, "generation", &error_gen);
	if (self->gen == 0)
		g_debug ("Unable to read generation: %s", error_gen->message);

	/* read the first block of firmware to get the is-native attribute */
	if (!fu_thunderbolt_device_read_status_block (self, error))
		return FALSE;

	/* determine if host controller or not */
	if (parent_name != NULL && g_str_has_prefix (parent_name, "domain")) {
		self->device_type = FU_THUNDERBOLT_DEVICE_TYPE_HOST_CONTROLLER;
		fu_device_add_flag (device, FWUPD_DEVICE_FLAG_INTERNAL);
		fu_device_set_summary (device, "Unmatched performance for high-speed I/O");
	} else {
		tmp = fu_udev_device_get_sysfs_attr (FU_UDEV_DEVICE (self), "device_name", NULL);
	}

	/* set the controller name */
	if (tmp == NULL)
		tmp = fu_thunderbolt_device_type_to_string (self);
	fu_device_set_name (device, tmp);

	/* set vendor string */
	tmp = fu_udev_device_get_sysfs_attr (FU_UDEV_DEVICE (self), "vendor_name", error);
	if (tmp == NULL)
		return FALSE;
	fu_device_set_vendor (device, tmp);

	if (fu_device_get_version (device) == NULL)
		fu_thunderbolt_device_check_safe_mode (self);

	if (self->safe_mode) {
		fu_device_set_update_error (device, "Device is in safe mode");
	} else {
		g_autofree gchar *device_id = NULL;
		g_autofree gchar *domain_id = NULL;
		if (fu_thunderbolt_device_can_update (self)) {
			g_autofree gchar *vendor_id = NULL;
			g_autofree gchar *domain = g_path_get_basename (self->devpath);
			/* USB4 controllers don't have a concept of legacy vs native
			 * so don't try to read a native attribute from their NVM */
			if (self->device_type == FU_THUNDERBOLT_DEVICE_TYPE_HOST_CONTROLLER
			    && self->gen < 4) {
				domain_id = g_strdup_printf ("TBT-%04x%04x%s-controller%s",
							     (guint) vid,
							     (guint) did,
							     self->is_native ? "-native" : "",
							     domain);
			}
			vendor_id = g_strdup_printf ("TBT:0x%04X", (guint) vid);
			fu_device_add_vendor_id (device, vendor_id);
			device_id = g_strdup_printf ("TBT-%04x%04x%s",
						     (guint) vid,
						     (guint) did,
						     self->is_native ? "-native" : "");
			fu_device_add_flag (device, FWUPD_DEVICE_FLAG_DUAL_IMAGE);

			/* check if device is authorized */
			if (!fu_thunderbolt_device_check_authorized (self, error))
				return FALSE;

		} else {
			device_id = g_strdup ("TBT-fixed");
		}
		fu_device_add_instance_id (device, device_id);
		if (domain_id != NULL)
			fu_device_add_instance_id (device, domain_id);
	}

	/* determine if we can update on unplug */
	if (fu_udev_device_get_sysfs_attr (FU_UDEV_DEVICE (device),
					     "nvm_authenticate_on_disconnect",
					     NULL) != NULL) {
		self->auth_method = "nvm_authenticate_on_disconnect";
		/* flushes image */
		fu_device_add_flag (device, FWUPD_DEVICE_FLAG_USABLE_DURING_UPDATE);
		/* forces the device to write to authenticate on disconnect attribute */
		fu_device_remove_flag (device, FWUPD_DEVICE_FLAG_SKIPS_RESTART);
		/* control the order of activation (less relevant; install too though) */
		fu_device_add_flag (device, FWUPD_DEVICE_FLAG_INSTALL_PARENT_FIRST);
	} else {
		fu_device_add_internal_flag (device, FU_DEVICE_INTERNAL_FLAG_REPLUG_MATCH_GUID);
	}

	return TRUE;
}

static gboolean
fu_thunderbolt_device_setup_retimer (FuDevice *device, GError **error)
{
	FuThunderboltDevice *self = FU_THUNDERBOLT_DEVICE (device);
	guint16 did;
	guint16 vid;
	g_autofree gchar *instance = NULL;

	/* as defined in PCIe 4.0 spec */
	fu_device_set_summary (device, "A physical layer protocol-aware, software-transparent extension device "
				        "that forms two separate electrical link segments");
	fu_device_set_name (device, fu_thunderbolt_device_type_to_string (self));
	fu_device_add_flag (device, FWUPD_DEVICE_FLAG_UPDATABLE);
	fu_device_add_flag (device, FWUPD_DEVICE_FLAG_DUAL_IMAGE);
	fu_device_add_flag (device, FWUPD_DEVICE_FLAG_INTERNAL);
	vid = fu_udev_device_get_vendor (FU_UDEV_DEVICE (self));
	if (vid == 0x0) {
		g_set_error_literal (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_NOT_SUPPORTED,
				     "missing vendor id");
		return FALSE;
	}

	did = fu_udev_device_get_model (FU_UDEV_DEVICE (self));
	if (did == 0x0) {
		g_set_error_literal (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_NOT_SUPPORTED,
				     "missing device id");
		return FALSE;

	}

	instance = g_strdup_printf ("TBT-%04x%04x-retimer%s",
				    (guint) vid,
				    (guint) did,
				    fu_device_get_physical_id (device));
	fu_device_add_instance_id (device, instance);

	/* hardcoded for now:
	 * 1. unsure if ID_VENDOR_FROM_DATABASE works in this instance
	 * 2. we don't recognize anyone else yet
	 */
	if (fu_device_get_vendor (device) == NULL)
		fu_device_set_vendor (device, "Intel");

	return TRUE;
}

static gboolean
fu_thunderbolt_device_setup (FuDevice *device, GError **error)
{
	FuThunderboltDevice *self = FU_THUNDERBOLT_DEVICE (device);
	g_autoptr(GError) error_version = NULL;

	self->devpath = g_strdup (fu_udev_device_get_sysfs_path (FU_UDEV_DEVICE (device)));

	/* try to read the version */
	if (!fu_thunderbolt_device_get_version (self, &error_version)) {
		if (g_error_matches (error_version, FWUPD_ERROR, FWUPD_ERROR_NOT_SUPPORTED)) {
			g_propagate_error (error, g_steal_pointer (&error_version));
			return FALSE;
		}
		g_debug ("%s", error_version->message);
	}

	/* default behavior */
	self->auth_method = "nvm_authenticate";

	/* configure differences between retimer and controller */
	if (self->device_type == FU_THUNDERBOLT_DEVICE_TYPE_RETIMER)
		return fu_thunderbolt_device_setup_retimer (device, error);
	return fu_thunderbolt_device_setup_controller (device, error);
}

static gboolean
fu_thunderbolt_device_activate (FuDevice *device, GError **error)
{
	FuUdevDevice *udev = FU_UDEV_DEVICE (device);

	return fu_udev_device_write_sysfs (udev, "nvm_authenticate", "1", error);
}

static gboolean
fu_thunderbolt_device_authenticate (FuDevice *device, GError **error)
{
	FuThunderboltDevice *self = FU_THUNDERBOLT_DEVICE (device);
	FuUdevDevice *udev = FU_UDEV_DEVICE (device);

	return fu_udev_device_write_sysfs (udev, self->auth_method, "1", error);
}

static gboolean
fu_thunderbolt_device_flush_update (FuDevice *device, GError **error)
{
	FuThunderboltDevice *self = FU_THUNDERBOLT_DEVICE (device);
	FuUdevDevice *udev = FU_UDEV_DEVICE (device);

	return fu_udev_device_write_sysfs (udev, self->auth_method, "2", error);
}

static gboolean
fu_thunderbolt_device_attach (FuDevice *device, GError **error)
{
	const gchar *attribute;
	guint64 status;

	/* now check if the update actually worked */
	attribute = fu_udev_device_get_sysfs_attr (FU_UDEV_DEVICE (device),
						   "nvm_authenticate",
						   error);
	if (attribute == NULL)
		return FALSE;
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

static gboolean
fu_thunderbolt_device_rescan (FuDevice *device, GError **error)
{
	FuThunderboltDevice *self = FU_THUNDERBOLT_DEVICE (device);

	/* refresh updatability */
	if (!fu_thunderbolt_device_check_authorized (self, error))
		return FALSE;

	/* refresh the version */
	return fu_thunderbolt_device_get_version (self, error);
}

static gboolean
fu_thunderbolt_device_write_data(FuThunderboltDevice *self,
				 GBytes *blob_fw,
				 FuProgress *progress,
				 GError **error)
{
	gsize fw_size;
	gsize nwritten;
	gssize n;
	g_autoptr(GFile) nvmem = NULL;
	g_autoptr(GOutputStream) os = NULL;

	nvmem = fu_thunderbolt_device_find_nvmem (self, FALSE, error);
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
	fu_progress_set_percentage_full(progress, nwritten, fw_size);

	do {
		g_autoptr(GBytes) fw_data = NULL;

		fw_data = fu_common_bytes_new_offset (blob_fw,
						      nwritten,
						      fw_size - nwritten,
						      error);
		if (fw_data == NULL)
			return FALSE;

		n = g_output_stream_write_bytes (os,
						 fw_data,
						 NULL,
						 error);
		if (n < 0)
			return FALSE;

		nwritten += n;
		fu_progress_set_percentage_full(progress, nwritten, fw_size);

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

static FuFirmware *
fu_thunderbolt_device_prepare_firmware (FuDevice *device,
					GBytes *fw,
					FwupdInstallFlags flags,
					GError **error)
{
	FuThunderboltDevice *self = FU_THUNDERBOLT_DEVICE (device);
	g_autoptr(FuThunderboltFirmwareUpdate) firmware = fu_thunderbolt_firmware_update_new ();
	g_autoptr(FuThunderboltFirmware) firmware_old = fu_thunderbolt_firmware_new ();
	g_autoptr(GBytes) controller_fw = NULL;
	g_autoptr(GFile) nvmem = NULL;

	/* parse */
	if (!fu_firmware_parse (FU_FIRMWARE (firmware), fw, flags, error))
		return NULL;

	/* get current NVMEM */
	nvmem = fu_thunderbolt_device_find_nvmem (self, TRUE, error);
	if (nvmem == NULL)
		return NULL;
	controller_fw = g_file_load_bytes (nvmem, NULL, NULL, error);
	if (controller_fw == NULL)
		return NULL;
	if (!fu_firmware_parse (FU_FIRMWARE (firmware_old), controller_fw, flags, error))
		return NULL;
	if (fu_thunderbolt_firmware_is_host (FU_THUNDERBOLT_FIRMWARE (firmware)) !=
	    fu_thunderbolt_firmware_is_host (firmware_old)) {
		g_set_error (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_INVALID_FILE,
			     "incorrect firmware mode, got %s, expected %s",
			     fu_thunderbolt_firmware_is_host (FU_THUNDERBOLT_FIRMWARE (firmware)) ? "host" : "device",
			     fu_thunderbolt_firmware_is_host (firmware_old) ? "host" : "device");
		return NULL;
	}
	if (fu_thunderbolt_firmware_get_vendor_id (FU_THUNDERBOLT_FIRMWARE (firmware)) !=
	    fu_thunderbolt_firmware_get_vendor_id (firmware_old)) {
		g_set_error (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_INVALID_FILE,
			     "incorrect device vendor, got 0x%04x, expected 0x%04x",
			     fu_thunderbolt_firmware_get_vendor_id (FU_THUNDERBOLT_FIRMWARE (firmware)),
			     fu_thunderbolt_firmware_get_vendor_id (firmware_old));
		return NULL;
	}
	if (fu_thunderbolt_firmware_get_device_id (FU_THUNDERBOLT_FIRMWARE (firmware)) !=
	    fu_thunderbolt_firmware_get_device_id (firmware_old)) {
		g_set_error (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_INVALID_FILE,
			     "incorrect device type, got 0x%04x, expected 0x%04x",
			     fu_thunderbolt_firmware_get_device_id (FU_THUNDERBOLT_FIRMWARE (firmware)),
			     fu_thunderbolt_firmware_get_device_id (firmware_old));
		return NULL;
	}
	if ((flags & FWUPD_INSTALL_FLAG_IGNORE_VID_PID) == 0) {
		if (fu_thunderbolt_firmware_get_model_id (FU_THUNDERBOLT_FIRMWARE (firmware)) !=
		    fu_thunderbolt_firmware_get_model_id (firmware_old)) {
			g_set_error (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_INVALID_FILE,
				     "incorrect device model, got 0x%04x, expected 0x%04x",
				     fu_thunderbolt_firmware_get_model_id (FU_THUNDERBOLT_FIRMWARE (firmware)),
				     fu_thunderbolt_firmware_get_model_id (firmware_old));
			return NULL;
		}
		/* old firmware has PD but new doesn't (we don't care about other way around) */
		if (fu_thunderbolt_firmware_get_has_pd (firmware_old) &&
		    !fu_thunderbolt_firmware_get_has_pd (FU_THUNDERBOLT_FIRMWARE (firmware))) {
			g_set_error_literal (error,
					     FWUPD_ERROR,
					     FWUPD_ERROR_INVALID_FILE,
					     "incorrect PD section");
			return NULL;
		}
		if (fu_thunderbolt_firmware_get_flash_size (FU_THUNDERBOLT_FIRMWARE (firmware)) !=
		    fu_thunderbolt_firmware_get_flash_size (firmware_old)) {
			g_set_error_literal (error,
					     FWUPD_ERROR,
					     FWUPD_ERROR_INVALID_FILE,
					     "incorrect flash size");
			return NULL;
		}
	}

	/* success */
	return FU_FIRMWARE (g_steal_pointer (&firmware));
}

static gboolean
fu_thunderbolt_device_write_firmware(FuDevice *device,
				     FuFirmware *firmware,
				     FuProgress *progress,
				     FwupdInstallFlags flags,
				     GError **error)
{
	FuThunderboltDevice *self = FU_THUNDERBOLT_DEVICE (device);
	g_autoptr(GBytes) blob_fw = NULL;

	/* get default image */
	blob_fw = fu_firmware_get_bytes (firmware, error);
	if (blob_fw == NULL)
		return FALSE;

	fu_device_set_status (device, FWUPD_STATUS_DEVICE_WRITE);
	if (!fu_thunderbolt_device_write_data(self, blob_fw, progress, error)) {
		g_prefix_error (error,
				"could not write firmware to thunderbolt device at %s: ",
				self->devpath);
		return FALSE;
	}

	/* flush the image if supported by kernel and/or device */
	if (fu_device_has_flag (device, FWUPD_DEVICE_FLAG_USABLE_DURING_UPDATE)) {
		if (!fu_thunderbolt_device_flush_update (device, error))
			return FALSE;
		fu_device_add_flag (device, FWUPD_DEVICE_FLAG_NEEDS_ACTIVATION);
	}

	/* using an active delayed activation flow later (either shutdown or another plugin) */
	if (fu_device_has_flag (device, FWUPD_DEVICE_FLAG_SKIPS_RESTART)) {
		g_debug ("Skipping Thunderbolt reset per quirk request");
		fu_device_add_flag (device, FWUPD_DEVICE_FLAG_NEEDS_ACTIVATION);
		return TRUE;
	}

	/* authenticate (possibly on unplug if device supports it) */
	if (!fu_thunderbolt_device_authenticate (FU_DEVICE (self), error)) {
		g_prefix_error (error, "could not start thunderbolt device upgrade: ");
		return FALSE;
	}

	/* whether to wait for a device replug or not */
	if (!fu_device_has_flag (device, FWUPD_DEVICE_FLAG_USABLE_DURING_UPDATE)) {
		fu_device_set_status (device, FWUPD_STATUS_DEVICE_RESTART);
		fu_device_set_remove_delay (device, FU_PLUGIN_THUNDERBOLT_UPDATE_TIMEOUT);
		fu_device_add_flag (device, FWUPD_DEVICE_FLAG_WAIT_FOR_REPLUG);
	}

	return TRUE;
}

static void
fu_thunderbolt_device_init (FuThunderboltDevice *self)
{
	fu_device_add_flag (FU_DEVICE (self), FWUPD_DEVICE_FLAG_REQUIRE_AC);
	fu_device_add_icon (FU_DEVICE (self), "thunderbolt");
	fu_device_add_protocol (FU_DEVICE (self), "com.intel.thunderbolt");
	fu_device_set_version_format (FU_DEVICE (self), FWUPD_VERSION_FORMAT_PAIR);
}

static void
fu_thunderbolt_device_finalize (GObject *object)
{
	FuThunderboltDevice *self = FU_THUNDERBOLT_DEVICE (object);
	G_OBJECT_CLASS (fu_thunderbolt_device_parent_class)->finalize (object);
	g_free (self->devpath);
}

static void
fu_thunderbolt_device_class_init (FuThunderboltDeviceClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	FuDeviceClass *klass_device = FU_DEVICE_CLASS (klass);
	object_class->finalize = fu_thunderbolt_device_finalize;
	klass_device->activate = fu_thunderbolt_device_activate;
	klass_device->to_string = fu_thunderbolt_device_to_string;
	klass_device->setup = fu_thunderbolt_device_setup;
	klass_device->prepare_firmware = fu_thunderbolt_device_prepare_firmware;
	klass_device->write_firmware = fu_thunderbolt_device_write_firmware;
	klass_device->attach = fu_thunderbolt_device_attach;
	klass_device->rescan = fu_thunderbolt_device_rescan;
	klass_device->probe = fu_thunderbolt_device_probe;
}
