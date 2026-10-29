/*
 * Copyright 2026 Mario Limonciello <superm1@gmail.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#define G_LOG_DOMAIN "FuLinuxFwUploadDevice"

#include "config.h"

#include "fu-common.h"
#include "fu-linux-fw-upload-device-private.h"
#include "fu-string.h"
#include "fu-udev-device-private.h"

/**
 * FuLinuxFwUploadDevice
 *
 * A device exposed by the kernel firmware upload framework, which is found at
 * `/sys/class/firmware/<name>`. This is usually the child of the "real" device, so plugins would
 * typically find this using fu_device_get_backend_parent_with_subsystem() or by enumerating the
 * children of the device, and then either use it as a proxy or derive from this class.
 *
 * The standard `FuDevice->write_firmware()` copies the image to the kernel and then waits for the
 * driver to finish programming the device. The time allowed for this is twice the value of
 * fu_device_get_install_duration(), or ten minutes if that has not been set.
 *
 * Activating the new firmware, for instance a reboot or a reset of the device, is device-specific
 * and is left to the plugin.
 *
 * See also: #FuUdevDevice
 */

#define FU_LINUX_FW_UPLOAD_DEVICE_POLL_INTERVAL	  100	 /* ms */
#define FU_LINUX_FW_UPLOAD_DEVICE_TIMEOUT_DEFAULT 600000 /* ms */
#define FU_LINUX_FW_UPLOAD_DEVICE_WRITE_TIMEOUT	  30000	 /* ms */

G_DEFINE_TYPE(FuLinuxFwUploadDevice, fu_linux_fw_upload_device, FU_TYPE_UDEV_DEVICE)

static gboolean
fu_linux_fw_upload_device_probe(FuDevice *device, GError **error)
{
	const gchar *attrs_required[] = {"status", "error", "remaining_size", "cancel"};
	const gchar *sysfs_path;
	g_autofree gchar *physical_id = NULL;
	g_auto(GStrv) sysfs_parts = NULL;
	g_autoptr(GPtrArray) attrs = NULL;

	/* FuUdevDevice */
	if (!FU_DEVICE_CLASS(fu_linux_fw_upload_device_parent_class)->probe(device, error))
		return FALSE;

	/* the kernel also creates devices in this class for the request_firmware() fallback, which
	 * look similar but have no way to upload anything */
	attrs = fu_udev_device_list_sysfs(FU_UDEV_DEVICE(device), error);
	if (attrs == NULL)
		return FALSE;
	for (guint i = 0; i < G_N_ELEMENTS(attrs_required); i++) {
		if (!g_ptr_array_find_with_equal_func(attrs,
						      attrs_required[i],
						      g_str_equal,
						      NULL)) {
			g_set_error(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_NOT_SUPPORTED,
				    "no %s attribute, so not a firmware upload device",
				    attrs_required[i]);
			return FALSE;
		}
	}

	/* the parent can be any subsystem, and the name is chosen by the driver, so use our own
	 * path */
	sysfs_path = fu_udev_device_get_sysfs_path(FU_UDEV_DEVICE(device));
	if (sysfs_path == NULL) {
		g_set_error_literal(error, FWUPD_ERROR, FWUPD_ERROR_NOT_SUPPORTED, "no sysfs path");
		return FALSE;
	}
	sysfs_parts = g_strsplit(sysfs_path, "/sys", 2);
	if (sysfs_parts[1] == NULL) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_NOT_SUPPORTED,
			    "sysfs path %s is not in /sys",
			    sysfs_path);
		return FALSE;
	}
	physical_id = g_strdup_printf("DEVPATH=%s", sysfs_parts[1]);
	fu_device_set_physical_id(device, physical_id);
	return TRUE;
}

/**
 * fu_linux_fw_upload_device_get_status:
 * @self: a #FuLinuxFwUploadDevice
 * @error: (nullable): optional return location for an error
 *
 * Reads the current progress of the upload.
 *
 * Returns: a #FuFwUploadStatus, or %FU_FW_UPLOAD_STATUS_IDLE on error
 *
 * Since: 2.2.1
 **/
FuFwUploadStatus
fu_linux_fw_upload_device_get_status(FuLinuxFwUploadDevice *self, GError **error)
{
	FuFwUploadStatus status;
	g_autofree gchar *str = NULL;

	g_return_val_if_fail(FU_IS_LINUX_FW_UPLOAD_DEVICE(self), FU_FW_UPLOAD_STATUS_IDLE);
	g_return_val_if_fail(error == NULL || *error == NULL, FU_FW_UPLOAD_STATUS_IDLE);

	str = fu_udev_device_read_sysfs(FU_UDEV_DEVICE(self),
					"status",
					FU_UDEV_DEVICE_ATTR_READ_TIMEOUT_DEFAULT,
					error);
	if (str == NULL)
		return FU_FW_UPLOAD_STATUS_IDLE;
	status = fu_fw_upload_status_from_string(str);
	if (status == FU_FW_UPLOAD_STATUS_IDLE && g_strcmp0(str, "idle") != 0) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_NOT_SUPPORTED,
			    "unknown firmware upload status: %s",
			    str);
		return FU_FW_UPLOAD_STATUS_IDLE;
	}
	return status;
}

/**
 * fu_linux_fw_upload_device_get_remaining_size:
 * @self: a #FuLinuxFwUploadDevice
 * @remaining_size: (out): the number of bytes the driver has not yet written
 * @error: (nullable): optional return location for an error
 *
 * Reads the number of bytes that the driver still has to transfer to the device. This is only
 * updated by the kernel after each chunk, and not at all while the device is programming.
 *
 * Returns: %TRUE for success
 *
 * Since: 2.2.1
 **/
gboolean
fu_linux_fw_upload_device_get_remaining_size(FuLinuxFwUploadDevice *self,
					     guint64 *remaining_size,
					     GError **error)
{
	g_autofree gchar *str = NULL;

	g_return_val_if_fail(FU_IS_LINUX_FW_UPLOAD_DEVICE(self), FALSE);
	g_return_val_if_fail(remaining_size != NULL, FALSE);
	g_return_val_if_fail(error == NULL || *error == NULL, FALSE);

	str = fu_udev_device_read_sysfs(FU_UDEV_DEVICE(self),
					"remaining_size",
					FU_UDEV_DEVICE_ATTR_READ_TIMEOUT_DEFAULT,
					error);
	if (str == NULL)
		return FALSE;
	return fu_strtoull(str, remaining_size, 0, G_MAXUINT64, FU_INTEGER_BASE_10, error);
}

/**
 * fu_linux_fw_upload_device_cancel:
 * @self: a #FuLinuxFwUploadDevice
 * @error: (nullable): optional return location for an error
 *
 * Requests that the driver cancels the upload. This is only a request, and the driver may refuse
 * if the device is already being programmed. A successful cancel will cause
 * `FuDevice->write_firmware()` to fail with an error of %FWUPD_ERROR_NOTHING_TO_DO.
 *
 * Returns: %TRUE for success
 *
 * Since: 2.2.1
 **/
gboolean
fu_linux_fw_upload_device_cancel(FuLinuxFwUploadDevice *self, GError **error)
{
	g_return_val_if_fail(FU_IS_LINUX_FW_UPLOAD_DEVICE(self), FALSE);
	g_return_val_if_fail(error == NULL || *error == NULL, FALSE);
	return fu_udev_device_write_sysfs(FU_UDEV_DEVICE(self),
					  "cancel",
					  "1",
					  FU_LINUX_FW_UPLOAD_DEVICE_WRITE_TIMEOUT,
					  error);
}

static gboolean
fu_linux_fw_upload_device_set_error_from_kernel(const gchar *str, GError **error)
{
	FuFwUploadError err_kernel;
	const gchar *err_str = str;
	const gchar *status_str = NULL;
	g_auto(GStrv) split = NULL;
	const FuErrorMapEntry entries[] = {
	    {FU_FW_UPLOAD_ERROR_NONE, FWUPD_ERROR_LAST, NULL},
	    {FU_FW_UPLOAD_ERROR_HW_ERROR, FWUPD_ERROR_INTERNAL, "hardware error"},
	    {FU_FW_UPLOAD_ERROR_TIMEOUT, FWUPD_ERROR_TIMED_OUT, "timeout"},
	    {FU_FW_UPLOAD_ERROR_USER_ABORT, FWUPD_ERROR_NOTHING_TO_DO, "aborted by the user"},
	    {FU_FW_UPLOAD_ERROR_DEVICE_BUSY, FWUPD_ERROR_BUSY, "device busy"},
	    {FU_FW_UPLOAD_ERROR_INVALID_FILE_SIZE, FWUPD_ERROR_INVALID_FILE, "invalid file size"},
	    {FU_FW_UPLOAD_ERROR_READ_WRITE_ERROR, FWUPD_ERROR_WRITE, "read or write error"},
	    {FU_FW_UPLOAD_ERROR_FLASH_WEAROUT, FWUPD_ERROR_WRITE, "flash wearout"},
	    {FU_FW_UPLOAD_ERROR_FIRMWARE_INVALID, FWUPD_ERROR_INVALID_FILE, "firmware invalid"},
	};

	/* "<status>:<error>" */
	split = g_strsplit(str, ":", 2);
	if (g_strv_length(split) == 2) {
		status_str = split[0];
		err_str = split[1];
	}

	/* an unknown string would otherwise look like %FU_FW_UPLOAD_ERROR_NONE */
	err_kernel = fu_fw_upload_error_from_string(err_str);
	if (err_kernel == FU_FW_UPLOAD_ERROR_NONE && g_strcmp0(err_str, "none") != 0) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INTERNAL,
			    "unknown firmware upload error: %s",
			    str);
		return FALSE;
	}
	if (fu_error_map_entry_to_gerror(err_kernel, entries, G_N_ELEMENTS(entries), error))
		return TRUE;
	if (status_str != NULL)
		g_prefix_error(error, "firmware upload failed during %s: ", status_str);
	else
		g_prefix_error_literal(error, "firmware upload failed: ");
	return FALSE;
}

/* discards the partial image in the kernel; this does nothing if it already started the upload */
static void
fu_linux_fw_upload_device_abort_best_effort(FuLinuxFwUploadDevice *self)
{
	g_autoptr(GError) error_local = NULL;
	if (!fu_udev_device_write_sysfs(FU_UDEV_DEVICE(self),
					"loading",
					"-1",
					FU_LINUX_FW_UPLOAD_DEVICE_WRITE_TIMEOUT,
					&error_local))
		g_debug("failed to abort firmware upload: %s", error_local->message);
}

/* the driver may refuse, e.g. when already programming, but there is nothing more we can do */
static void
fu_linux_fw_upload_device_cancel_best_effort(FuLinuxFwUploadDevice *self)
{
	g_autoptr(GError) error_local = NULL;
	if (!fu_linux_fw_upload_device_cancel(self, &error_local))
		g_debug("failed to cancel firmware upload: %s", error_local->message);
}

/* how long to wait for the driver, which a plugin can lengthen using the install duration */
static guint
fu_linux_fw_upload_device_get_timeout(FuLinuxFwUploadDevice *self)
{
	guint duration = fu_device_get_install_duration(FU_DEVICE(self));
	if (duration == 0)
		return FU_LINUX_FW_UPLOAD_DEVICE_TIMEOUT_DEFAULT;
	return duration * 2 * 1000;
}

typedef struct {
	guint64 total;
	FuProgress *progress;
} FuLinuxFwUploadDeviceWaitHelper;

static gboolean
fu_linux_fw_upload_device_wait_cb(FuDevice *device, gpointer user_data, GError **error)
{
	FuLinuxFwUploadDevice *self = FU_LINUX_FW_UPLOAD_DEVICE(device);
	FuLinuxFwUploadDeviceWaitHelper *helper = (FuLinuxFwUploadDeviceWaitHelper *)user_data;
	FuFwUploadStatus status;
	g_autoptr(GError) error_local = NULL;

	status = fu_linux_fw_upload_device_get_status(self, &error_local);
	if (error_local != NULL) {
		g_propagate_error(error, g_steal_pointer(&error_local));
		return FALSE;
	}
	if (status == FU_FW_UPLOAD_STATUS_IDLE) {
		fu_progress_set_percentage(helper->progress, 100);
		return TRUE;
	}

	/* only updated after each chunk, and not at all when programming */
	if (status == FU_FW_UPLOAD_STATUS_TRANSFERRING) {
		guint64 remaining = 0;
		if (!fu_linux_fw_upload_device_get_remaining_size(self, &remaining, error))
			return FALSE;
		if (remaining <= helper->total) {
			fu_progress_set_percentage_full(helper->progress,
							helper->total - remaining,
							helper->total);
		}
	}

	/* not an error as such, so the retry will try again */
	g_set_error(error,
		    FWUPD_ERROR,
		    FWUPD_ERROR_BUSY,
		    "firmware upload still %s",
		    fu_fw_upload_status_to_string(status));
	return FALSE;
}

static gboolean
fu_linux_fw_upload_device_wait_for_idle(FuLinuxFwUploadDevice *self,
					guint64 total,
					FuProgress *progress,
					GError **error)
{
	FuLinuxFwUploadDeviceWaitHelper helper = {.total = total, .progress = progress};
	guint timeout = fu_linux_fw_upload_device_get_timeout(self);
	g_autoptr(GError) error_local = NULL;

	if (fu_device_retry_full(FU_DEVICE(self),
				 fu_linux_fw_upload_device_wait_cb,
				 MAX(timeout / FU_LINUX_FW_UPLOAD_DEVICE_POLL_INTERVAL, 1),
				 FU_LINUX_FW_UPLOAD_DEVICE_POLL_INTERVAL,
				 &helper,
				 &error_local))
		return TRUE;

	/* don't leave the kernel flashing behind our back */
	fu_linux_fw_upload_device_cancel_best_effort(self);

	/* running out of retries is a timeout, rather than a failure to read the status */
	if (g_error_matches(error_local, FWUPD_ERROR, FWUPD_ERROR_BUSY)) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_TIMED_OUT,
			    "timed out waiting for firmware upload after %ums",
			    timeout);
		return FALSE;
	}
	g_propagate_error(error, g_steal_pointer(&error_local));
	return FALSE;
}

static gboolean
fu_linux_fw_upload_device_write_firmware(FuDevice *device,
					 FuFirmware *firmware,
					 FuProgress *progress,
					 FwupdInstallFlags flags,
					 GError **error)
{
	FuLinuxFwUploadDevice *self = FU_LINUX_FW_UPLOAD_DEVICE(device);
	g_autofree gchar *err_str = NULL;
	g_autoptr(GBytes) fw = NULL;
	g_autoptr(GError) error_local = NULL;

	fw = fu_firmware_get_bytes(firmware, error);
	if (fw == NULL)
		return FALSE;

	/* the kernel refuses to upload an empty image */
	if (g_bytes_get_size(fw) == 0) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INVALID_FILE,
				    "firmware upload image is empty");
		return FALSE;
	}

	fu_progress_set_id(progress, G_STRLOC);
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_WRITE, 10, "write");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_WRITE, 90, "flash");

	/* discard anything stale, and start receiving */
	if (!fu_udev_device_write_sysfs(FU_UDEV_DEVICE(self),
					"loading",
					"1",
					FU_LINUX_FW_UPLOAD_DEVICE_WRITE_TIMEOUT,
					error))
		return FALSE;

	/* copy the image, and then abort the receive on failure so the kernel frees the buffer */
	if (!fu_udev_device_write_sysfs_bytes(FU_UDEV_DEVICE(self),
					      "data",
					      fw,
					      FU_LINUX_FW_UPLOAD_DEVICE_WRITE_TIMEOUT,
					      &error_local)) {
		fu_linux_fw_upload_device_abort_best_effort(self);
		g_propagate_error(error, g_steal_pointer(&error_local));
		return FALSE;
	}

	/* this returns as soon as the work is queued */
	if (!fu_udev_device_write_sysfs(FU_UDEV_DEVICE(self),
					"loading",
					"0",
					FU_LINUX_FW_UPLOAD_DEVICE_WRITE_TIMEOUT,
					error)) {
		fu_linux_fw_upload_device_abort_best_effort(self);
		return FALSE;
	}
	fu_progress_step_done(progress);

	/* wait for the driver */
	if (!fu_linux_fw_upload_device_wait_for_idle(self,
						     g_bytes_get_size(fw),
						     fu_progress_get_child(progress),
						     error))
		return FALSE;
	fu_progress_step_done(progress);

	/* empty on success */
	err_str = fu_udev_device_read_sysfs(FU_UDEV_DEVICE(self),
					    "error",
					    FU_UDEV_DEVICE_ATTR_READ_TIMEOUT_DEFAULT,
					    error);
	if (err_str == NULL)
		return FALSE;
	if (err_str[0] == '\0')
		return TRUE;
	return fu_linux_fw_upload_device_set_error_from_kernel(err_str, error);
}

static void
fu_linux_fw_upload_device_init(FuLinuxFwUploadDevice *self)
{
	/* if the device is removed there is no point waiting for the driver any longer */
	fu_device_retry_add_recovery(FU_DEVICE(self), FWUPD_ERROR, FWUPD_ERROR_NOT_FOUND, NULL);
}

static void
fu_linux_fw_upload_device_class_init(FuLinuxFwUploadDeviceClass *klass)
{
	FuDeviceClass *device_class = FU_DEVICE_CLASS(klass);
	device_class->probe = fu_linux_fw_upload_device_probe;
	device_class->write_firmware = fu_linux_fw_upload_device_write_firmware;
}
