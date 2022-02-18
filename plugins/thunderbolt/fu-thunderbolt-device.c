/*
 * Copyright (C) 2017 Christian J. Kellner <christian@kellner.me>
 * Copyright (C) 2020 Mario Limonciello <mario.limonciello@dell.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include <fwupdplugin.h>

#include <errno.h>
#include <fcntl.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include "fu-thunderbolt-device.h"
#include "fu-thunderbolt-firmware-update.h"

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
	guint64 status;
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
	status = g_ascii_strtoull(attribute, NULL, 16);
	if (status == G_MAXUINT64 && errno == ERANGE) {
		g_set_error(error,
			    G_IO_ERROR,
			    g_io_error_from_errno(errno),
			    "failed to read 'authorized: %s",
			    g_strerror(errno));
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
		g_debug("Attempt %u: Failed to read NVM version", i);
		g_usleep(TBT_NVM_RETRY_TIMEOUT * 1000);
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

	version = g_strdup_printf("%02x.%02x",
				  (guint)g_ascii_strtoull(split[0], NULL, 16),
				  (guint)g_ascii_strtoull(split[1], NULL, 16));
	fu_device_set_version(FU_DEVICE(self), version);
	return TRUE;
}

static void
fu_thunderbolt_device_to_string(FuDevice *device, guint idt, GString *str)
{
	FuThunderboltDevice *self = FU_THUNDERBOLT_DEVICE(device);
	FuThunderboltDevicePrivate *priv = GET_PRIVATE(self);

	/* FuUdevDevice->to_string */
	FU_DEVICE_CLASS(fu_thunderbolt_device_parent_class)->to_string(device, idt, str);

	fu_common_string_append_kv(str, idt, "AuthMethod", priv->auth_method);
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

	return fu_udev_device_write_sysfs(udev, "nvm_authenticate", "1", error);
}

static gboolean
fu_thunderbolt_device_authenticate(FuDevice *device, GError **error)
{
	FuThunderboltDevice *self = FU_THUNDERBOLT_DEVICE(device);
	FuThunderboltDevicePrivate *priv = GET_PRIVATE(self);
	FuUdevDevice *udev = FU_UDEV_DEVICE(device);

	return fu_udev_device_write_sysfs(udev, priv->auth_method, "1", error);
}

static gboolean
fu_thunderbolt_device_flush_update(FuDevice *device, GError **error)
{
	FuThunderboltDevice *self = FU_THUNDERBOLT_DEVICE(device);
	FuThunderboltDevicePrivate *priv = GET_PRIVATE(self);
	FuUdevDevice *udev = FU_UDEV_DEVICE(device);

	return fu_udev_device_write_sysfs(udev, priv->auth_method, "2", error);
}

static gboolean
fu_thunderbolt_device_attach(FuDevice *device, FuProgress *progress, GError **error)
{
	const gchar *attribute;
	guint64 status;

	/* now check if the update actually worked */
	attribute =
	    fu_udev_device_get_sysfs_attr(FU_UDEV_DEVICE(device), "nvm_authenticate", error);
	if (attribute == NULL)
		return FALSE;
	status = g_ascii_strtoull(attribute, NULL, 16);
	if (status == G_MAXUINT64 && errno == ERANGE) {
		g_set_error(error,
			    G_IO_ERROR,
			    g_io_error_from_errno(errno),
			    "failed to read 'nvm_authenticate: %s",
			    g_strerror(errno));
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

	nvmem = fu_thunderbolt_device_find_nvmem(self, FALSE, error);
	if (nvmem == NULL)
		return FALSE;

	os = (GOutputStream *)g_file_append_to(nvmem, G_FILE_CREATE_NONE, NULL, error);

	if (os == NULL)
		return FALSE;

	nwritten = 0;
	fw_size = g_bytes_get_size(blob_fw);

	do {
		g_autoptr(GBytes) fw_data = NULL;

		fw_data = fu_common_bytes_new_offset(blob_fw, nwritten, fw_size - nwritten, error);
		if (fw_data == NULL)
			return FALSE;

		n = g_output_stream_write_bytes(os, fw_data, NULL, error);
		if (n < 0)
			return FALSE;

		nwritten += n;
		fu_progress_set_percentage_full(progress, nwritten, fw_size);

	} while (nwritten < fw_size);

	if (nwritten != fw_size) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_WRITE,
				    "Could not write all data to nvmem");
		return FALSE;
	}

	return g_output_stream_close(os, NULL, error);
}

static FuFirmware *
fu_thunderbolt_device_prepare_firmware(FuDevice *device,
				       GBytes *fw,
				       FwupdInstallFlags flags,
				       GError **error)
{
	FuThunderboltDevice *self = FU_THUNDERBOLT_DEVICE(device);
	g_autoptr(FuThunderboltFirmwareUpdate) firmware = fu_thunderbolt_firmware_update_new();
	g_autoptr(FuThunderboltFirmware) firmware_old = fu_thunderbolt_firmware_new();
	g_autoptr(GBytes) controller_fw = NULL;
	g_autoptr(GFile) nvmem = NULL;

	/* parse */
	if (!fu_firmware_parse(FU_FIRMWARE(firmware), fw, flags, error))
		return NULL;

	/* get current NVMEM */
	nvmem = fu_thunderbolt_device_find_nvmem(self, TRUE, error);
	if (nvmem == NULL)
		return NULL;
	controller_fw = g_file_load_bytes(nvmem, NULL, NULL, error);
	if (controller_fw == NULL)
		return NULL;
	if (!fu_firmware_parse(FU_FIRMWARE(firmware_old), controller_fw, flags, error))
		return NULL;
	if (fu_thunderbolt_firmware_is_host(FU_THUNDERBOLT_FIRMWARE(firmware)) !=
	    fu_thunderbolt_firmware_is_host(firmware_old)) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INVALID_FILE,
			    "incorrect firmware mode, got %s, expected %s",
			    fu_thunderbolt_firmware_is_host(FU_THUNDERBOLT_FIRMWARE(firmware))
				? "host"
				: "device",
			    fu_thunderbolt_firmware_is_host(firmware_old) ? "host" : "device");
		return NULL;
	}
	if (fu_thunderbolt_firmware_get_vendor_id(FU_THUNDERBOLT_FIRMWARE(firmware)) !=
	    fu_thunderbolt_firmware_get_vendor_id(firmware_old)) {
		g_set_error(
		    error,
		    FWUPD_ERROR,
		    FWUPD_ERROR_INVALID_FILE,
		    "incorrect device vendor, got 0x%04x, expected 0x%04x",
		    fu_thunderbolt_firmware_get_vendor_id(FU_THUNDERBOLT_FIRMWARE(firmware)),
		    fu_thunderbolt_firmware_get_vendor_id(firmware_old));
		return NULL;
	}
	if (fu_thunderbolt_firmware_get_device_id(FU_THUNDERBOLT_FIRMWARE(firmware)) !=
	    fu_thunderbolt_firmware_get_device_id(firmware_old)) {
		g_set_error(
		    error,
		    FWUPD_ERROR,
		    FWUPD_ERROR_INVALID_FILE,
		    "incorrect device type, got 0x%04x, expected 0x%04x",
		    fu_thunderbolt_firmware_get_device_id(FU_THUNDERBOLT_FIRMWARE(firmware)),
		    fu_thunderbolt_firmware_get_device_id(firmware_old));
		return NULL;
	}
	if ((flags & FWUPD_INSTALL_FLAG_IGNORE_VID_PID) == 0) {
		if (fu_thunderbolt_firmware_get_model_id(FU_THUNDERBOLT_FIRMWARE(firmware)) !=
		    fu_thunderbolt_firmware_get_model_id(firmware_old)) {
			g_set_error(
			    error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INVALID_FILE,
			    "incorrect device model, got 0x%04x, expected 0x%04x",
			    fu_thunderbolt_firmware_get_model_id(FU_THUNDERBOLT_FIRMWARE(firmware)),
			    fu_thunderbolt_firmware_get_model_id(firmware_old));
			return NULL;
		}
		/* old firmware has PD but new doesn't (we don't care about other way around) */
		if (fu_thunderbolt_firmware_get_has_pd(firmware_old) &&
		    !fu_thunderbolt_firmware_get_has_pd(FU_THUNDERBOLT_FIRMWARE(firmware))) {
			g_set_error_literal(error,
					    FWUPD_ERROR,
					    FWUPD_ERROR_INVALID_FILE,
					    "incorrect PD section");
			return NULL;
		}
		if (fu_thunderbolt_firmware_get_flash_size(FU_THUNDERBOLT_FIRMWARE(firmware)) !=
		    fu_thunderbolt_firmware_get_flash_size(firmware_old)) {
			g_set_error_literal(error,
					    FWUPD_ERROR,
					    FWUPD_ERROR_INVALID_FILE,
					    "incorrect flash size");
			return NULL;
		}
	}

	/* success */
	return FU_FIRMWARE(g_steal_pointer(&firmware));
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
	if (fu_device_has_flag(device, FWUPD_DEVICE_FLAG_SKIPS_RESTART)) {
		g_debug("Skipping Thunderbolt reset per quirk request");
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
	g_autoptr(FuUdevDevice) udev_parent = NULL;

	/* FuUdevDevice->probe */
	if (!FU_DEVICE_CLASS(fu_thunderbolt_device_parent_class)->probe(device, error))
		return FALSE;

	/* if the PCI ID is Intel then it's signed, no idea otherwise */
	udev_parent = fu_udev_device_get_parent_with_subsystem(FU_UDEV_DEVICE(device), "pci");
	if (udev_parent != NULL) {
		if (!fu_device_probe(FU_DEVICE(udev_parent), error))
			return FALSE;
		if (fu_udev_device_get_vendor(udev_parent) == 0x8086)
			fu_device_add_flag(device, FWUPD_DEVICE_FLAG_SIGNED_PAYLOAD);
	}

	/* success */
	return TRUE;
}

static void
fu_thunderbolt_device_set_progress(FuDevice *self, FuProgress *progress)
{
	fu_progress_set_id(progress, G_STRLOC);
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_RESTART, 0); /* detach */
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_WRITE, 100); /* write */
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_RESTART, 0); /* attach */
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_BUSY, 0);	/* reload */
}

static void
fu_thunderbolt_device_init(FuThunderboltDevice *self)
{
	FuThunderboltDevicePrivate *priv = GET_PRIVATE(self);
	priv->auth_method = "nvm_authenticate";
	fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_UPDATABLE);
	fu_device_add_icon(FU_DEVICE(self), "thunderbolt");
	fu_device_add_protocol(FU_DEVICE(self), "com.intel.thunderbolt");
	fu_device_set_version_format(FU_DEVICE(self), FWUPD_VERSION_FORMAT_PAIR);
}

static void
fu_thunderbolt_device_class_init(FuThunderboltDeviceClass *klass)
{
	FuDeviceClass *klass_device = FU_DEVICE_CLASS(klass);
	klass_device->activate = fu_thunderbolt_device_activate;
	klass_device->to_string = fu_thunderbolt_device_to_string;
	klass_device->probe = fu_thunderbolt_device_probe;
	klass_device->prepare_firmware = fu_thunderbolt_device_prepare_firmware;
	klass_device->write_firmware = fu_thunderbolt_device_write_firmware;
	klass_device->attach = fu_thunderbolt_device_attach;
	klass_device->rescan = fu_thunderbolt_device_rescan;
	klass_device->set_progress = fu_thunderbolt_device_set_progress;
}
