/*
 * Copyright 2026 Mario Limonciello <superm1@gmail.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include <fwupdplugin.h>

#include "fu-context-private.h"
#include "fu-device-private.h"
#include "fu-linux-fw-upload-device-private.h"
#include "fu-udev-device-private.h"

/* the base64 of FU_LINUX_FW_UPLOAD_DEVICE_TEST_FW */
#define FU_LINUX_FW_UPLOAD_DEVICE_TEST_FW_B64 "q80="

static FuLinuxFwUploadDevice *
fu_linux_fw_upload_device_test_new(void)
{
	g_autoptr(FuContext) ctx = fu_context_new();
	g_autoptr(FuLinuxFwUploadDevice) self =
	    g_object_new(FU_TYPE_LINUX_FW_UPLOAD_DEVICE, "context", ctx, NULL);

	fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_EMULATED);
	fu_device_set_fwupd_version(FU_DEVICE(self), PACKAGE_VERSION);
	return g_steal_pointer(&self);
}

static void
fu_linux_fw_upload_device_test_add_event(FuLinuxFwUploadDevice *self,
					 const gchar *id,
					 const gchar *data)
{
	g_autoptr(FuDeviceEvent) event = fu_device_event_new(id);
	if (data != NULL)
		fu_device_event_set_str(event, "Data", data);
	fu_device_add_event(FU_DEVICE(self), event);
}

static void
fu_linux_fw_upload_device_test_add_start(FuLinuxFwUploadDevice *self)
{
	fu_linux_fw_upload_device_test_add_event(self, "WriteAttr:Attr=loading,Data=1", NULL);
	fu_linux_fw_upload_device_test_add_event(
	    self,
	    "WriteAttr:Attr=data,Data=" FU_LINUX_FW_UPLOAD_DEVICE_TEST_FW_B64,
	    NULL);
	fu_linux_fw_upload_device_test_add_event(self, "WriteAttr:Attr=loading,Data=0", NULL);
}

static gboolean
fu_linux_fw_upload_device_test_write(FuLinuxFwUploadDevice *self,
				     GBytes *fw,
				     FuProgress *progress,
				     GError **error)
{
	g_autoptr(FuFirmware) firmware = fu_firmware_new_from_bytes(fw);
	return fu_device_write_firmware(FU_DEVICE(self),
					firmware,
					progress,
					FWUPD_INSTALL_FLAG_NONE,
					error);
}

static void
fu_linux_fw_upload_device_status_func(void)
{
	FuFwUploadStatus status;
	guint64 remaining = 0;
	gboolean ret;
	g_autoptr(FuLinuxFwUploadDevice) dev = fu_linux_fw_upload_device_test_new();
	g_autoptr(GError) error = NULL;

	fu_linux_fw_upload_device_test_add_event(dev, "ReadAttr:Attr=status", "programming");
	status = fu_linux_fw_upload_device_get_status(dev, &error);
	g_assert_no_error(error);
	g_assert_cmpint(status, ==, FU_FW_UPLOAD_STATUS_PROGRAMMING);

	fu_linux_fw_upload_device_test_add_event(dev, "ReadAttr:Attr=remaining_size", "1024");
	ret = fu_linux_fw_upload_device_get_remaining_size(dev, &remaining, &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	g_assert_cmpint(remaining, ==, 1024);

	/* something the kernel might add in the future */
	fu_linux_fw_upload_device_test_add_event(dev, "ReadAttr:Attr=status", "levitating");
	status = fu_linux_fw_upload_device_get_status(dev, &error);
	g_assert_error(error, FWUPD_ERROR, FWUPD_ERROR_NOT_SUPPORTED);
	g_assert_cmpint(status, ==, FU_FW_UPLOAD_STATUS_IDLE);
}

static void
fu_linux_fw_upload_device_cancel_func(void)
{
	gboolean ret;
	g_autoptr(FuLinuxFwUploadDevice) dev = fu_linux_fw_upload_device_test_new();
	g_autoptr(GError) error = NULL;

	/* no event for the write */
	ret = fu_linux_fw_upload_device_cancel(dev, &error);
	g_assert_error(error, FWUPD_ERROR, FWUPD_ERROR_NOT_FOUND);
	g_assert_false(ret);
	g_clear_error(&error);

	fu_linux_fw_upload_device_test_add_event(dev, "WriteAttr:Attr=cancel,Data=1", NULL);
	ret = fu_linux_fw_upload_device_cancel(dev, &error);
	g_assert_no_error(error);
	g_assert_true(ret);
}

static void
fu_linux_fw_upload_device_write_success_func(void)
{
	gboolean ret;
	g_autoptr(FuLinuxFwUploadDevice) dev = fu_linux_fw_upload_device_test_new();
	g_autoptr(FuProgress) progress = fu_progress_new(G_STRLOC);
	g_autoptr(GBytes) fw = g_bytes_new_static("\xAB\xCD", 2);
	g_autoptr(GError) error = NULL;

	fu_linux_fw_upload_device_test_add_start(dev);
	fu_linux_fw_upload_device_test_add_event(dev, "ReadAttr:Attr=status", "preparing");
	fu_linux_fw_upload_device_test_add_event(dev, "ReadAttr:Attr=status", "transferring");
	fu_linux_fw_upload_device_test_add_event(dev, "ReadAttr:Attr=remaining_size", "1");
	fu_linux_fw_upload_device_test_add_event(dev, "ReadAttr:Attr=status", "programming");
	fu_linux_fw_upload_device_test_add_event(dev, "ReadAttr:Attr=status", "idle");
	fu_linux_fw_upload_device_test_add_event(dev, "ReadAttr:Attr=error", "");

	ret = fu_linux_fw_upload_device_test_write(dev, fw, progress, &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	g_assert_cmpint(fu_progress_get_percentage(progress), ==, 100);
}

static void
fu_linux_fw_upload_device_write_empty_func(void)
{
	gboolean ret;
	g_autoptr(FuLinuxFwUploadDevice) dev = fu_linux_fw_upload_device_test_new();
	g_autoptr(FuProgress) progress = fu_progress_new(G_STRLOC);
	g_autoptr(GBytes) fw = g_bytes_new_static("", 0);
	g_autoptr(GError) error = NULL;

	ret = fu_linux_fw_upload_device_test_write(dev, fw, progress, &error);
	g_assert_error(error, FWUPD_ERROR, FWUPD_ERROR_INVALID_FILE);
	g_assert_false(ret);
}

static void
fu_linux_fw_upload_device_write_data_fail_func(void)
{
	gboolean ret;
	g_autoptr(FuLinuxFwUploadDevice) dev = fu_linux_fw_upload_device_test_new();
	g_autoptr(FuProgress) progress = fu_progress_new(G_STRLOC);
	g_autoptr(GBytes) fw = g_bytes_new_static("\xAB\xCD", 2);
	g_autoptr(GError) error = NULL;

	/* no event for the data write, so the kernel buffer is thrown away */
	fu_linux_fw_upload_device_test_add_event(dev, "WriteAttr:Attr=loading,Data=1", NULL);
	fu_linux_fw_upload_device_test_add_event(dev, "WriteAttr:Attr=loading,Data=-1", NULL);

	ret = fu_linux_fw_upload_device_test_write(dev, fw, progress, &error);
	g_assert_error(error, FWUPD_ERROR, FWUPD_ERROR_NOT_FOUND);
	g_assert_false(ret);
}

static void
fu_linux_fw_upload_device_write_errors_func(void)
{
	const struct {
		const gchar *kernel;
		gint code;
		const gchar *message;
	} map[] = {
	    {"transferring:user-abort", FWUPD_ERROR_NOTHING_TO_DO, "aborted by the user"},
	    {"preparing:device-busy", FWUPD_ERROR_BUSY, "device busy"},
	    {"preparing:invalid-file-size", FWUPD_ERROR_INVALID_FILE, "invalid file size"},
	    {"preparing:firmware-invalid", FWUPD_ERROR_INVALID_FILE, "firmware invalid"},
	    {"programming:timeout", FWUPD_ERROR_TIMED_OUT, "timeout"},
	    {"transferring:read-write-error", FWUPD_ERROR_WRITE, "read or write error"},
	    {"programming:flash-wearout", FWUPD_ERROR_WRITE, "flash wearout"},
	    {"programming:hw-error", FWUPD_ERROR_INTERNAL, "hardware error"},
	    {"programming:made-up-error", FWUPD_ERROR_INTERNAL, "made-up-error"},
	};

	for (guint i = 0; i < G_N_ELEMENTS(map); i++) {
		gboolean ret;
		g_autoptr(FuLinuxFwUploadDevice) dev = fu_linux_fw_upload_device_test_new();
		g_autoptr(FuProgress) progress = fu_progress_new(G_STRLOC);
		g_autoptr(GBytes) fw = g_bytes_new_static("\xAB\xCD", 2);
		g_autoptr(GError) error = NULL;

		g_debug("testing %s", map[i].kernel);
		fu_linux_fw_upload_device_test_add_start(dev);
		fu_linux_fw_upload_device_test_add_event(dev, "ReadAttr:Attr=status", "idle");
		fu_linux_fw_upload_device_test_add_event(dev, "ReadAttr:Attr=error", map[i].kernel);

		ret = fu_linux_fw_upload_device_test_write(dev, fw, progress, &error);
		g_assert_error(error, FWUPD_ERROR, map[i].code);
		g_assert_false(ret);
		g_assert_nonnull(g_strrstr(error->message, map[i].message));
	}
}

static void
fu_linux_fw_upload_device_write_error_no_status_func(void)
{
	gboolean ret;
	g_autoptr(FuLinuxFwUploadDevice) dev = fu_linux_fw_upload_device_test_new();
	g_autoptr(FuProgress) progress = fu_progress_new(G_STRLOC);
	g_autoptr(GBytes) fw = g_bytes_new_static("\xAB\xCD", 2);
	g_autoptr(GError) error = NULL;

	/* not a valid "<status>:<error>" */
	fu_linux_fw_upload_device_test_add_start(dev);
	fu_linux_fw_upload_device_test_add_event(dev, "ReadAttr:Attr=status", "idle");
	fu_linux_fw_upload_device_test_add_event(dev, "ReadAttr:Attr=error", "flash-wearout");

	ret = fu_linux_fw_upload_device_test_write(dev, fw, progress, &error);
	g_assert_error(error, FWUPD_ERROR, FWUPD_ERROR_WRITE);
	g_assert_false(ret);
}

static void
fu_linux_fw_upload_device_timeout_func(void)
{
	gboolean ret;
	g_autoptr(FuLinuxFwUploadDevice) dev = fu_linux_fw_upload_device_test_new();
	g_autoptr(FuProgress) progress = fu_progress_new(G_STRLOC);
	g_autoptr(GBytes) fw = g_bytes_new_static("\xAB\xCD", 2);
	g_autoptr(GError) error = NULL;

	/* twice this, polled every 100ms, so no waiting thanks to the emulation */
	fu_device_set_install_duration(FU_DEVICE(dev), 1);

	/* never goes idle, and the driver ignores the cancel */
	fu_linux_fw_upload_device_test_add_start(dev);
	fu_linux_fw_upload_device_test_add_event(dev, "ReadAttr:Attr=status", "programming");

	ret = fu_linux_fw_upload_device_test_write(dev, fw, progress, &error);
	g_assert_error(error, FWUPD_ERROR, FWUPD_ERROR_TIMED_OUT);
	g_assert_false(ret);
}

static void
fu_linux_fw_upload_device_cancel_on_poll_error_func(void)
{
	const struct {
		const gchar *status; /* NULL for no status file at all */
		const gchar *missing;
	} map[] = {
	    {NULL, "status"},
	    {"transferring", "remaining_size"},
	};

	for (guint i = 0; i < G_N_ELEMENTS(map); i++) {
		gboolean ret;
		g_autofree gchar *cancel = NULL;
		g_autofree gchar *cancel_data = NULL;
		g_autoptr(FuContext) ctx = fu_context_new();
		g_autoptr(FuLinuxFwUploadDevice) dev = NULL;
		g_autoptr(FuProgress) progress = fu_progress_new(G_STRLOC);
		g_autoptr(FuTemporaryDirectory) tmpdir = NULL;
		g_autoptr(GBytes) fw = g_bytes_new_static("\xAB\xCD", 2);
		g_autoptr(GError) error = NULL;
		const gchar *attrs[] = {"loading", "data", "cancel", "status", NULL};

		tmpdir = fu_temporary_directory_new("fw-upload", &error);
		g_assert_no_error(error);
		g_assert_nonnull(tmpdir);

		/* a real, non-emulated sysfs directory where the poll read fails after queueing */
		for (guint j = 0; attrs[j] != NULL; j++) {
			g_autofree gchar *fn = fu_temporary_directory_build(tmpdir, attrs[j], NULL);
			const gchar *data = "";
			if (g_strcmp0(attrs[j], "status") == 0) {
				if (map[i].status == NULL)
					continue;
				data = map[i].status;
			}
			ret = g_file_set_contents(fn, data, -1, &error);
			g_assert_no_error(error);
			g_assert_true(ret);
		}
		dev = g_object_new(FU_TYPE_LINUX_FW_UPLOAD_DEVICE,
				   "context",
				   ctx,
				   "backend-id",
				   fu_temporary_directory_get_path(tmpdir),
				   NULL);

		ret = fu_linux_fw_upload_device_test_write(dev, fw, progress, &error);
		g_assert_error(error, FWUPD_ERROR, FWUPD_ERROR_NOT_FOUND);
		g_assert_false(ret);
		g_clear_error(&error);

		/* the kernel must have been asked to stop, rather than left flashing */
		cancel = fu_temporary_directory_build(tmpdir, "cancel", NULL);
		ret = g_file_get_contents(cancel, &cancel_data, NULL, &error);
		g_assert_no_error(error);
		g_assert_true(ret);
		g_assert_cmpstr(cancel_data, ==, "1");
		g_debug("%s was missing", map[i].missing);
	}
}

static void
fu_linux_fw_upload_device_write_loading_fail_aborts_func(void)
{
	gboolean ret;
	g_autoptr(FuLinuxFwUploadDevice) dev = fu_linux_fw_upload_device_test_new();
	g_autoptr(FuProgress) progress = fu_progress_new(G_STRLOC);
	g_autoptr(GBytes) fw = g_bytes_new_static("\xAB\xCD", 2);
	g_autoptr(GError) error = NULL;

	/* strict ordering lets us see if the abort was sent, as the next event lookup will say
	 * there are no more events rather than complaining that it found the abort */
	fu_device_add_private_flag(FU_DEVICE(dev), FU_DEVICE_PRIVATE_FLAG_STRICT_EMULATION_ORDER);
	fu_linux_fw_upload_device_test_add_event(dev, "WriteAttr:Attr=loading,Data=1", NULL);
	fu_linux_fw_upload_device_test_add_event(
	    dev,
	    "WriteAttr:Attr=data,Data=" FU_LINUX_FW_UPLOAD_DEVICE_TEST_FW_B64,
	    NULL);
	fu_linux_fw_upload_device_test_add_event(dev, "WriteAttr:Attr=loading,Data=-1", NULL);

	/* no event for loading=0, so that fails */
	ret = fu_linux_fw_upload_device_test_write(dev, fw, progress, &error);
	g_assert_error(error, FWUPD_ERROR, FWUPD_ERROR_NOT_FOUND);
	g_assert_false(ret);
	g_clear_error(&error);

	/* the abort consumed the last event */
	ret = fu_linux_fw_upload_device_cancel(dev, &error);
	g_assert_error(error, FWUPD_ERROR, FWUPD_ERROR_NOT_FOUND);
	g_assert_false(ret);
	g_assert_nonnull(g_strrstr(error->message, "no more events"));
}

static FuLinuxFwUploadDevice *
fu_linux_fw_upload_device_test_new_for_probe(const gchar *attrs)
{
	g_autoptr(FuContext) ctx = fu_context_new();
	g_autoptr(FuBackend) backend = NULL;
	g_autoptr(FuLinuxFwUploadDevice) self = NULL;

	backend = g_object_new(FU_TYPE_BACKEND,
			       "context",
			       ctx,
			       "name",
			       "udev",
			       "device-gtype",
			       FU_TYPE_UDEV_DEVICE,
			       NULL);
	self = g_object_new(FU_TYPE_LINUX_FW_UPLOAD_DEVICE, "context", ctx, NULL);
	fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_EMULATED);
	fu_device_set_fwupd_version(FU_DEVICE(self), PACKAGE_VERSION);
	fu_device_set_backend(FU_DEVICE(self), backend);
	fu_device_set_backend_id(FU_DEVICE(self),
				 "/sys/devices/pci0000:00/0000:00:01.0/firmware/fw0");
	fu_udev_device_set_subsystem(FU_UDEV_DEVICE(self), "firmware");
	fu_linux_fw_upload_device_test_add_event(self, "ListAttr", attrs);
	return g_steal_pointer(&self);
}

static void
fu_linux_fw_upload_device_probe_func(void)
{
	gboolean ret;
	g_autoptr(FuLinuxFwUploadDevice) dev = NULL;
	g_autoptr(GError) error = NULL;

	/* a real fw_upload device */
	dev = fu_linux_fw_upload_device_test_new_for_probe(
	    "loading\ndata\nstatus\nerror\nremaining_size\ncancel\ntimeout\nuevent");
	ret = fu_device_probe(FU_DEVICE(dev), &error);
	g_assert_no_error(error);
	g_assert_true(ret);
	g_assert_cmpstr(fu_device_get_physical_id(FU_DEVICE(dev)),
			==,
			"DEVPATH=/devices/pci0000:00/0000:00:01.0/firmware/fw0");
}

static void
fu_linux_fw_upload_device_probe_fallback_func(void)
{
	const gchar *attrs[] = {
	    /* the request_firmware() fallback, which only has the loading half */
	    "loading\ndata\ntimeout\nuevent",
	    /* missing exactly one of the required attributes */
	    "loading\ndata\nerror\nremaining_size\ncancel",
	    "loading\ndata\nstatus\nremaining_size\ncancel",
	    "loading\ndata\nstatus\nerror\ncancel",
	    "loading\ndata\nstatus\nerror\nremaining_size",
	};

	for (guint i = 0; i < G_N_ELEMENTS(attrs); i++) {
		gboolean ret;
		g_autoptr(FuLinuxFwUploadDevice) dev =
		    fu_linux_fw_upload_device_test_new_for_probe(attrs[i]);
		g_autoptr(GError) error = NULL;

		ret = fu_device_probe(FU_DEVICE(dev), &error);
		g_assert_error(error, FWUPD_ERROR, FWUPD_ERROR_NOT_SUPPORTED);
		g_assert_false(ret);
	}
}

static void
fu_linux_fw_upload_device_write_device_removed_func(void)
{
	gboolean ret;
	g_autoptr(FuLinuxFwUploadDevice) dev = fu_linux_fw_upload_device_test_new();
	g_autoptr(FuProgress) progress = fu_progress_new(G_STRLOC);
	g_autoptr(GBytes) fw = g_bytes_new_static("\xAB\xCD", 2);
	g_autoptr(GError) error = NULL;

	/* the status can no longer be read, which the emulation reports as not found */
	fu_linux_fw_upload_device_test_add_start(dev);

	ret = fu_linux_fw_upload_device_test_write(dev, fw, progress, &error);
	g_assert_error(error, FWUPD_ERROR, FWUPD_ERROR_NOT_FOUND);
	g_assert_false(ret);
	g_assert_nonnull(g_strrstr(error->message, "device recovery not possible"));
}

static void
fu_linux_fw_upload_device_enums_func(void)
{
	g_assert_cmpstr(fu_fw_upload_status_to_string(FU_FW_UPLOAD_STATUS_IDLE), ==, "idle");
	g_assert_cmpint(fu_fw_upload_status_from_string("transferring"),
			==,
			FU_FW_UPLOAD_STATUS_TRANSFERRING);
	g_assert_cmpstr(fu_fw_upload_error_to_string(FU_FW_UPLOAD_ERROR_READ_WRITE_ERROR),
			==,
			"read-write-error");
	g_assert_cmpint(fu_fw_upload_error_from_string("firmware-invalid"),
			==,
			FU_FW_UPLOAD_ERROR_FIRMWARE_INVALID);
}

int
main(int argc, char **argv)
{
	g_test_init(&argc, &argv, NULL);
	g_test_add_func("/fwupd/linux-fw-upload-device/enums",
			fu_linux_fw_upload_device_enums_func);
	g_test_add_func("/fwupd/linux-fw-upload-device/status",
			fu_linux_fw_upload_device_status_func);
	g_test_add_func("/fwupd/linux-fw-upload-device/cancel",
			fu_linux_fw_upload_device_cancel_func);
	g_test_add_func("/fwupd/linux-fw-upload-device/write-success",
			fu_linux_fw_upload_device_write_success_func);
	g_test_add_func("/fwupd/linux-fw-upload-device/write-empty",
			fu_linux_fw_upload_device_write_empty_func);
	g_test_add_func("/fwupd/linux-fw-upload-device/write-data-fail",
			fu_linux_fw_upload_device_write_data_fail_func);
	g_test_add_func("/fwupd/linux-fw-upload-device/write-errors",
			fu_linux_fw_upload_device_write_errors_func);
	g_test_add_func("/fwupd/linux-fw-upload-device/write-error-no-status",
			fu_linux_fw_upload_device_write_error_no_status_func);
	g_test_add_func("/fwupd/linux-fw-upload-device/timeout",
			fu_linux_fw_upload_device_timeout_func);
	g_test_add_func("/fwupd/linux-fw-upload-device/write-loading-fail-aborts",
			fu_linux_fw_upload_device_write_loading_fail_aborts_func);
	g_test_add_func("/fwupd/linux-fw-upload-device/write-device-removed",
			fu_linux_fw_upload_device_write_device_removed_func);
	g_test_add_func("/fwupd/linux-fw-upload-device/probe",
			fu_linux_fw_upload_device_probe_func);
	g_test_add_func("/fwupd/linux-fw-upload-device/probe-fallback",
			fu_linux_fw_upload_device_probe_fallback_func);
	g_test_add_func("/fwupd/linux-fw-upload-device/cancel-on-poll-error",
			fu_linux_fw_upload_device_cancel_on_poll_error_func);
	return g_test_run();
}
