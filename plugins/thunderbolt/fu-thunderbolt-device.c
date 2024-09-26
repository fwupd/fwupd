/*
 * Copyright 2017 Christian J. Kellner <christian@kellner.me>
 * Copyright 2020 Mario Limonciello <mario.limonciello@dell.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include <errno.h>
#include <fcntl.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include "fu-thunderbolt-common.h"
#include "fu-thunderbolt-controller.h"
#include "fu-thunderbolt-device.h"

typedef struct {
	const gchar *auth_method;
} FuThunderboltDevicePrivate;

#define TBT_NVM_RETRY_TIMEOUT		     200   /* ms */
#define FU_PLUGIN_THUNDERBOLT_UPDATE_TIMEOUT 60000 /* ms */

G_DEFINE_TYPE_WITH_PRIVATE(FuThunderboltDevice, fu_thunderbolt_device, FU_TYPE_UDEV_DEVICE)

#define GET_PRIVATE(o) (fu_thunderbolt_device_get_instance_private(o))

GFile *
fu_thunderbolt_device_find_nvmem(FuThunderboltDevice *self, gboolean active, GError **error)
{
	const gchar *nvmem_dir = active ? "nvm_active" : "nvm_non_active";
	const gchar *name;
	const gchar *devpath = fu_udev_device_get_sysfs_path(FU_UDEV_DEVICE(self));
	g_autoptr(GDir) d = NULL;

	if (G_UNLIKELY(devpath == NULL)) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INTERNAL,
				    "Could not determine sysfs path for device");
		return NULL;
	}

	d = g_dir_open(devpath, 0, error);
	if (d == NULL)
		return NULL;

	while ((name = g_dir_read_name(d)) != NULL) {
		if (g_str_has_prefix(name, nvmem_dir)) {
			g_autoptr(GFile) parent = g_file_new_for_path(devpath);
			g_autoptr(GFile) nvm_dir = g_file_get_child(parent, name);
			return g_file_get_child(nvm_dir, "nvmem");
		}
	}

	g_set_error_literal(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_NOT_SUPPORTED,
			    "Could not find non-volatile memory location");
	return NULL;
}

gboolean
fu_thunderbolt_device_check_authorized(FuThunderboltDevice *self, GError **error)
{
	guint64 status = 0;
	g_autofree gchar *attribute = NULL;
	const gchar *devpath = fu_udev_device_get_sysfs_path(FU_UDEV_DEVICE(self));
	/* read directly from file to prevent udev caching */
	g_autofree gchar *safe_path = g_build_path("/", devpath, "authorized", NULL);

	if (!g_file_test(safe_path, G_FILE_TEST_EXISTS)) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_NOT_SUPPORTED,
				    "missing authorized attribute");
		return FALSE;
	}

	if (!g_file_get_contents(safe_path, &attribute, NULL, error))
		return FALSE;
	if (!fu_strtoull(attribute, &status, 0, G_MAXUINT64, FU_INTEGER_BASE_16, error)) {
		g_prefix_error(error, "failed to read authorized: ");
		return FALSE;
	}
	if (status == 1 || status == 2)
		fu_device_uninhibit(FU_DEVICE(self), "not-authorized");
	else
		fu_device_inhibit(FU_DEVICE(self), "not-authorized", "Not authorized");

	return TRUE;
}

gboolean
fu_thunderbolt_device_get_version(FuThunderboltDevice *self, GError **error)
{
	const gchar *devpath = fu_udev_device_get_sysfs_path(FU_UDEV_DEVICE(self));
	guint64 version_major = 0;
	guint64 version_minor = 0;
	g_auto(GStrv) split = NULL;
	g_autofree gchar *version_raw = NULL;
	g_autofree gchar *version = NULL;
	/* read directly from file to prevent udev caching */
	g_autofree gchar *safe_path = g_build_path("/", devpath, "nvm_version", NULL);

	if (!g_file_test(safe_path, G_FILE_TEST_EXISTS)) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_NOT_SUPPORTED,
				    "missing nvm_version attribute");
		return FALSE;
	}

	for (guint i = 0; i < 50; i++) {
		g_autoptr(GError) error_local = NULL;
		/* glib can't return a properly mapped -ENODATA but the
		 * kernel only returns -ENODATA or -EAGAIN */
		if (g_file_get_contents(safe_path, &version_raw, NULL, &error_local))
			break;
		g_debug("attempt %u: failed to read NVM version", i);
		fu_device_sleep(FU_DEVICE(self), TBT_NVM_RETRY_TIMEOUT);
		/* safe mode probably */
		if (g_error_matches(error_local, G_IO_ERROR, G_IO_ERROR_WOULD_BLOCK))
			break;
	}

	if (version_raw == NULL) {
		g_set_error_literal(error, FWUPD_ERROR, FWUPD_ERROR_INTERNAL, "failed to read NVM");
		return FALSE;
	}
	split = g_strsplit(version_raw, ".", -1);
	if (g_strv_length(split) != 2) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_NOT_SUPPORTED,
			    "invalid nvm_version format: %s",
			    version_raw);
		return FALSE;
	}
	if (!fu_strtoull(split[0], &version_major, 0, G_MAXUINT64, FU_INTEGER_BASE_16, error))
		return FALSE;
	if (!fu_strtoull(split[1], &version_minor, 0, G_MAXUINT64, FU_INTEGER_BASE_16, error))
		return FALSE;
	version = g_strdup_printf("%02x.%02x", (guint)version_major, (guint)version_minor);
	fu_device_set_version(FU_DEVICE(self), version);
	return TRUE;
}

static void
fu_thunderbolt_device_to_string(FuDevice *device, guint idt, GString *str)
{
	FuThunderboltDevice *self = FU_THUNDERBOLT_DEVICE(device);
	FuThunderboltDevicePrivate *priv = GET_PRIVATE(self);
	fwupd_codec_string_append(str, idt, "AuthMethod", priv->auth_method);
}

void
fu_thunderbolt_device_set_auth_method(FuThunderboltDevice *self, const gchar *auth_method)
{
	FuThunderboltDevicePrivate *priv = GET_PRIVATE(self);
	priv->auth_method = auth_method;
}

static gboolean
fu_thunderbolt_device_activate(FuDevice *device, FuProgress *progress, GError **error)
{
	FuUdevDevice *udev = FU_UDEV_DEVICE(device);

	return fu_udev_device_write_sysfs(udev,
					  "nvm_authenticate",
					  "1",
					  FU_THUNDERBOLT_DEVICE_WRITE_TIMEOUT,
					  error);
}

static gboolean
fu_thunderbolt_device_authenticate(FuDevice *device, GError **error)
{
	FuThunderboltDevice *self = FU_THUNDERBOLT_DEVICE(device);
	FuThunderboltDevicePrivate *priv = GET_PRIVATE(self);
	FuUdevDevice *udev = FU_UDEV_DEVICE(device);

	return fu_udev_device_write_sysfs(udev,
					  priv->auth_method,
					  "1",
					  FU_THUNDERBOLT_DEVICE_WRITE_TIMEOUT,
					  error);
}

static gboolean
fu_thunderbolt_device_flush_update(FuDevice *device, GError **error)
{
	FuThunderboltDevice *self = FU_THUNDERBOLT_DEVICE(device);
	FuThunderboltDevicePrivate *priv = GET_PRIVATE(self);
	FuUdevDevice *udev = FU_UDEV_DEVICE(device);

	return fu_udev_device_write_sysfs(udev,
					  priv->auth_method,
					  "2",
					  FU_THUNDERBOLT_DEVICE_WRITE_TIMEOUT,
					  error);
}

static gboolean
fu_thunderbolt_device_attach(FuDevice *device, FuProgress *progress, GError **error)
{
	g_autofree gchar *attr_nvm_authenticate = NULL;
	guint64 status = 0;

	/* now check if the update actually worked */
	attr_nvm_authenticate = fu_udev_device_read_sysfs(FU_UDEV_DEVICE(device),
							  "nvm_authenticate",
							  FU_UDEV_DEVICE_ATTR_READ_TIMEOUT_DEFAULT,
							  error);
	if (attr_nvm_authenticate == NULL)
		return FALSE;
	if (!fu_strtoull(attr_nvm_authenticate,
			 &status,
			 0,
			 G_MAXUINT64,
			 FU_INTEGER_BASE_16,
			 error)) {
		g_prefix_error(error, "failed to read nvm_authenticate: ");
		return FALSE;
	}

	/* anything else then 0x0 means we got an error */
	if (status != 0x0) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INTERNAL,
			    "update failed (status %" G_GINT64_MODIFIER "x)",
			    status);
		return FALSE;
	}

	return TRUE;
}

static gboolean
fu_thunderbolt_device_rescan(FuDevice *device, GError **error)
{
	FuThunderboltDevice *self = FU_THUNDERBOLT_DEVICE(device);

	/* refresh updatability */
	if (!fu_thunderbolt_device_check_authorized(self, error))
		return FALSE;

	/* refresh the version */
	return fu_thunderbolt_device_get_version(self, error);
}

static gboolean
fu_thunderbolt_device_write_stream(GOutputStream *ostream,
				   GBytes *bytes,
				   FuProgress *progress,
				   GError **error)
{
	gsize bufsz = g_bytes_get_size(bytes);
	gsize total_written = 0;

	do {
		gssize wrote;
		g_autoptr(GBytes) fw_data = NULL;
		fw_data = fu_bytes_new_offset(bytes, total_written, bufsz - total_written, error);
		if (fw_data == NULL)
			return FALSE;
		wrote = g_output_stream_write_bytes(ostream, fw_data, NULL, error);
		if (wrote < 0)
			return FALSE;
		total_written += wrote;
		fu_progress_set_percentage_full(progress, total_written, bufsz);
	} while (total_written < bufsz);
	if (total_written != bufsz) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_WRITE,
			    "only wrote 0x%x of 0x%x",
			    (guint)total_written,
			    (guint)bufsz);
		return FALSE;
	}

	/* success */
	return TRUE;
}

static gboolean
fu_thunderbolt_device_write_data(FuThunderboltDevice *self,
				 GBytes *blob_fw,
				 FuProgress *progress,
				 GError **error)
{
	g_autoptr(GFile) nvmem = NULL;
	g_autoptr(GOutputStream) ostream = NULL;

	nvmem = fu_thunderbolt_device_find_nvmem(self, FALSE, error);
	if (nvmem == NULL)
		return FALSE;
	ostream = (GOutputStream *)g_file_append_to(nvmem, G_FILE_CREATE_NONE, NULL, error);
	if (ostream == NULL)
		return FALSE;
	if (!fu_thunderbolt_device_write_stream(ostream, blob_fw, progress, error))
		return FALSE;
	return g_output_stream_close(ostream, NULL, error);
}

static FuFirmware *
fu_thunderbolt_device_prepare_firmware(FuDevice *device,
				       GInputStream *stream,
				       FuProgress *progress,
				       FwupdInstallFlags flags,
				       GError **error)
{
	FuThunderboltDevice *self = FU_THUNDERBOLT_DEVICE(device);
	g_autoptr(FuFirmware) firmware = NULL;

	/* parse */
	firmware = fu_firmware_new_from_gtypes(stream,
					       0x0,
					       flags,
					       error,
					       FU_TYPE_INTEL_THUNDERBOLT_FIRMWARE,
					       FU_TYPE_FIRMWARE,
					       G_TYPE_INVALID);
	if (firmware == NULL)
		return NULL;

	/* get current NVMEM */
	if (fu_firmware_has_flag(firmware, FU_FIRMWARE_FLAG_HAS_CHECK_COMPATIBLE)) {
		g_autoptr(FuFirmware) firmware_old = NULL;
		g_autoptr(GFile) nvmem = NULL;
		g_autoptr(GInputStream) controller_fw = NULL;

		fu_progress_set_status(progress, FWUPD_STATUS_DEVICE_READ);
		nvmem = fu_thunderbolt_device_find_nvmem(self, TRUE, error);
		if (nvmem == NULL)
			return NULL;
		controller_fw = G_INPUT_STREAM(g_file_read(nvmem, NULL, error));
		if (controller_fw == NULL)
			return NULL;
		firmware_old = fu_firmware_new_from_gtypes(controller_fw,
							   0x0,
							   flags,
							   error,
							   FU_TYPE_INTEL_THUNDERBOLT_NVM,
							   FU_TYPE_FIRMWARE,
							   G_TYPE_INVALID);
		if (firmware_old == NULL)
			return NULL;
		if (!fu_firmware_check_compatible(firmware_old, firmware, flags, error))
			return NULL;
	}

	/* success */
	return g_steal_pointer(&firmware);
}

static gboolean
fu_thunderbolt_device_write_firmware(FuDevice *device,
				     FuFirmware *firmware,
				     FuProgress *progress,
				     FwupdInstallFlags flags,
				     GError **error)
{
	FuThunderboltDevice *self = FU_THUNDERBOLT_DEVICE(device);
	g_autoptr(GBytes) blob_fw = NULL;

	/* get default image */
	blob_fw = fu_firmware_get_bytes(firmware, error);
	if (blob_fw == NULL)
		return FALSE;

	fu_progress_set_status(progress, FWUPD_STATUS_DEVICE_WRITE);
	if (!fu_thunderbolt_device_write_data(self, blob_fw, progress, error)) {
		g_prefix_error(error,
			       "could not write firmware to thunderbolt device at %s: ",
			       fu_udev_device_get_sysfs_path(FU_UDEV_DEVICE(self)));
		return FALSE;
	}

	/* flush the image if supported by kernel and/or device */
	if (fu_device_has_flag(device, FWUPD_DEVICE_FLAG_USABLE_DURING_UPDATE)) {
		if (!fu_thunderbolt_device_flush_update(device, error))
			return FALSE;
		fu_device_add_flag(device, FWUPD_DEVICE_FLAG_NEEDS_ACTIVATION);
	}

	/* using an active delayed activation flow later (either shutdown or another plugin) */
	if (fu_device_has_private_flag(device, FU_DEVICE_PRIVATE_FLAG_SKIPS_RESTART)) {
		g_debug("skipping Thunderbolt reset per quirk request");
		fu_device_add_flag(device, FWUPD_DEVICE_FLAG_NEEDS_ACTIVATION);
		return TRUE;
	}

	/* authenticate (possibly on unplug if device supports it) */
	if (!fu_thunderbolt_device_authenticate(FU_DEVICE(self), error)) {
		g_prefix_error(error, "could not start thunderbolt device upgrade: ");
		return FALSE;
	}

	/* whether to wait for a device replug or not */
	if (!fu_device_has_flag(device, FWUPD_DEVICE_FLAG_USABLE_DURING_UPDATE)) {
		fu_device_set_remove_delay(device, FU_PLUGIN_THUNDERBOLT_UPDATE_TIMEOUT);
		fu_progress_set_status(progress, FWUPD_STATUS_DEVICE_RESTART);
	}

	return TRUE;
}

static gboolean
fu_thunderbolt_device_probe(FuDevice *device, GError **error)
{
	g_autoptr(FuDevice) udev_parent = NULL;

	/* if the PCI ID is Intel then it's signed, no idea otherwise */
	udev_parent = fu_device_get_backend_parent_with_subsystem(device, "pci", NULL);
	if (udev_parent != NULL) {
		if (!fu_device_probe(udev_parent, error))
			return FALSE;
		if (fu_device_get_vid(udev_parent) == 0x8086)
			fu_device_add_flag(device, FWUPD_DEVICE_FLAG_SIGNED_PAYLOAD);
	}

	/* success */
	return TRUE;
}

static void
fu_thunderbolt_device_set_progress(FuDevice *self, FuProgress *progress)
{
	fu_progress_set_id(progress, G_STRLOC);
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_RESTART, 0, "detach");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_WRITE, 100, "write");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_RESTART, 0, "attach");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_BUSY, 0, "reload");
}

static void
fu_thunderbolt_device_init(FuThunderboltDevice *self)
{
	FuThunderboltDevicePrivate *priv = GET_PRIVATE(self);
	priv->auth_method = "nvm_authenticate";
	fu_device_add_icon(FU_DEVICE(self), "thunderbolt");
	fu_device_add_protocol(FU_DEVICE(self), "com.intel.thunderbolt");
}

static void
fu_thunderbolt_device_class_init(FuThunderboltDeviceClass *klass)
{
	FuDeviceClass *device_class = FU_DEVICE_CLASS(klass);
	device_class->activate = fu_thunderbolt_device_activate;
	device_class->to_string = fu_thunderbolt_device_to_string;
	device_class->probe = fu_thunderbolt_device_probe;
	device_class->prepare_firmware = fu_thunderbolt_device_prepare_firmware;
	device_class->write_firmware = fu_thunderbolt_device_write_firmware;
	device_class->attach = fu_thunderbolt_device_attach;
	device_class->rescan = fu_thunderbolt_device_rescan;
	device_class->set_progress = fu_thunderbolt_device_set_progress;
}
