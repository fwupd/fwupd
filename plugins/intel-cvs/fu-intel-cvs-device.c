/*
 * Copyright 2024 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include "fu-intel-cvs-device.h"
#include "fu-intel-cvs-firmware.h"
#include "fu-intel-cvs-struct.h"

#define FU_INTEL_CVS_DEVICE_SYSFS_TIMEOUT 500 /* ms */

struct _FuIntelCvsDevice {
	FuI2cDevice parent_instance;
	guint32 max_download_time;
	guint32 max_flash_time;
	guint32 max_retry_count;
};

G_DEFINE_TYPE(FuIntelCvsDevice, fu_intel_cvs_device, FU_TYPE_I2C_DEVICE)

static void
fu_intel_cvs_device_to_string(FuDevice *device, guint idt, GString *str)
{
	FuIntelCvsDevice *self = FU_INTEL_CVS_DEVICE(device);
	fwupd_codec_string_append_hex(str, idt, "MaxDownloadTime", self->max_download_time);
	fwupd_codec_string_append_hex(str, idt, "MaxFlashTime", self->max_flash_time);
	fwupd_codec_string_append_hex(str, idt, "MaxRetryCount", self->max_retry_count);
}

static gboolean
fu_intel_cvs_device_setup(FuDevice *device, GError **error)
{
	FuIntelCvsDevice *self = FU_INTEL_CVS_DEVICE(device);
	g_autofree gchar *version = NULL;
	g_autoptr(FuStructIntelCvsProbe) st_probe = NULL;
	g_autoptr(GBytes) blob = NULL;

	/* read and parse the status */
	blob = fu_udev_device_read_sysfs_bytes(FU_UDEV_DEVICE(self),
					       "cvs_ctrl_data_pre",
					       FU_STRUCT_INTEL_CVS_PROBE_SIZE,
					       FU_INTEL_CVS_DEVICE_SYSFS_TIMEOUT,
					       error);
	if (blob == NULL)
		return FALSE;
	st_probe = fu_struct_intel_cvs_probe_parse_bytes(blob, 0x0, error);
	if (st_probe == NULL)
		return FALSE;

	/* production, so no downgrades */
	if (fu_struct_intel_cvs_probe_get_dev_capabilities(st_probe) && 0)
		fu_device_add_flag(device, FWUPD_DEVICE_FLAG_ONLY_VERSION_UPGRADE);

	/* build the version */
	version = g_strdup_printf("%u.%u.%u.%u",
				  fu_struct_intel_cvs_probe_get_major(st_probe),
				  fu_struct_intel_cvs_probe_get_minor(st_probe),
				  fu_struct_intel_cvs_probe_get_hotfix(st_probe),
				  fu_struct_intel_cvs_probe_get_build(st_probe));
	fu_device_set_version(device, version);
	fu_device_set_vid(device, fu_struct_intel_cvs_probe_get_vid(st_probe));
	fu_device_set_pid(device, fu_struct_intel_cvs_probe_get_pid(st_probe));
	return fu_device_build_instance_id(device, error, "I2C", "NAME", "VID", "PID", NULL);
}

static void
fu_intel_cvs_device_vid_notify_cb(FuDevice *device, GParamSpec *pspec, gpointer user_data)
{
	fu_device_add_instance_u16(device, "VID", fu_device_get_vid(device));
	fu_device_build_vendor_id_u16(device, "USB", fu_device_get_vid(device));
}

static void
fu_intel_cvs_device_pid_notify_cb(FuDevice *device, GParamSpec *pspec, gpointer user_data)
{
	fu_device_add_instance_u16(device, "PID", fu_device_get_pid(device));
}

static FuFirmware *
fu_intel_cvs_device_prepare_firmware(FuDevice *device,
				     GInputStream *stream,
				     FuProgress *progress,
				     FwupdInstallFlags flags,
				     GError **error)
{
	FuIntelCvsDevice *self = FU_INTEL_CVS_DEVICE(device);
	g_autoptr(FuFirmware) firmware = fu_intel_cvs_firmware_new();

	/* check is compatible */
	if (fu_device_get_vid(FU_DEVICE(self)) !=
		fu_intel_cvs_firmware_get_vid(FU_INTEL_CVS_FIRMWARE(firmware)) ||
	    fu_device_get_pid(FU_DEVICE(self)) !=
		fu_intel_cvs_firmware_get_pid(FU_INTEL_CVS_FIRMWARE(firmware))) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INVALID_FILE,
			    "invalid firmware, got %04x:%04x, expected %04x:%04x",
			    fu_intel_cvs_firmware_get_vid(FU_INTEL_CVS_FIRMWARE(firmware)),
			    fu_intel_cvs_firmware_get_pid(FU_INTEL_CVS_FIRMWARE(firmware)),
			    fu_device_get_vid(FU_DEVICE(self)),
			    fu_device_get_pid(FU_DEVICE(self)));
		return NULL;
	}
	if (!fu_firmware_parse_stream(firmware, stream, 0x0, flags, error))
		return NULL;
	return g_steal_pointer(&firmware);
}

static gboolean
fu_intel_cvs_device_check_status_cb(FuDevice *device, gpointer user_data, GError **error)
{
	FuIntelCvsDevstate devstate;
	FuProgress *progress = FU_PROGRESS(user_data);
	g_autoptr(FuStructIntelCvsStatus) st_status = NULL;
	g_autoptr(GBytes) blob = NULL;

	/* read and parse the status */
	blob = fu_udev_device_read_sysfs_bytes(FU_UDEV_DEVICE(device),
					       "cvs_ctrl_data_fwupd",
					       FU_STRUCT_INTEL_CVS_PROBE_SIZE,
					       FU_INTEL_CVS_DEVICE_SYSFS_TIMEOUT,
					       error);
	if (blob == NULL)
		return FALSE;
	st_status = fu_struct_intel_cvs_status_parse_bytes(blob, 0x0, error);
	if (st_status == NULL)
		return FALSE;
	fu_progress_set_percentage_full(progress,
					fu_struct_intel_cvs_status_get_num_packets_sent(st_status),
					fu_struct_intel_cvs_status_get_total_packets(st_status));
	if (fu_struct_intel_cvs_status_get_fw_dl_finished(st_status) == 0) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INTERNAL,
				    "waiting for update to complete");
		return FALSE;
	}
	devstate = fu_struct_intel_cvs_status_get_dev_state(st_status);
	// FIXME?
	// fu_progress_set_status(helper->progress, FWUPD_STATUS_DEVICE_BUSY);
	if (devstate != FU_INTEL_CVS_DEVSTATE_UNKNOWN) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INTERNAL,
			    "failed; devstate was %s",
			    fu_intel_cvs_devstate_to_string(devstate));
		return FALSE;
	}

	/* success */
	return TRUE;
}

static gboolean
fu_intel_cvs_device_write_firmware(FuDevice *device,
				   FuFirmware *firmware,
				   FuProgress *progress,
				   FwupdInstallFlags flags,
				   GError **error)
{
	FuIntelCvsDevice *self = FU_INTEL_CVS_DEVICE(device);
	g_autoptr(FuChunkArray) chunks = NULL;
	g_autoptr(FuIOChannel) io_payload = NULL;
	g_autoptr(FuStructIntelCvsWrite) st_write = fu_struct_intel_cvs_write_new();
	g_autoptr(GInputStream) stream = NULL;

	/* get default image */
	stream = fu_firmware_get_stream(firmware, error);
	if (stream == NULL)
		return FALSE;

	/* write firmware stream into a virtual fd */
	io_payload = fu_io_channel_virtual_new("fwupd-cvs-plugin", error);
	if (io_payload == NULL)
		return FALSE;
	if (!fu_io_channel_write_stream(io_payload,
					stream,
					FU_INTEL_CVS_DEVICE_SYSFS_TIMEOUT,
					FU_IO_CHANNEL_FLAG_NONE,
					error)) {
		g_prefix_error(error, "failed to write payload to virtual stream: ");
		return FALSE;
	}
	if (!fu_io_channel_seek(io_payload, 0x0, error))
		return FALSE;

	/* write */
	fu_struct_intel_cvs_write_set_max_download_time(st_write, self->max_download_time);
	fu_struct_intel_cvs_write_set_max_flash_time(st_write, self->max_flash_time);
	fu_struct_intel_cvs_write_set_max_fwupd_retry_count(st_write, self->max_retry_count);
	fu_struct_intel_cvs_write_set_fw_bin_fd(st_write, fu_io_channel_unix_get_fd(io_payload));
	if (!fu_udev_device_write_sysfs_byte_array(FU_UDEV_DEVICE(self),
						   "cvs_ctrl_data_pre",
						   st_write,
						   FU_INTEL_CVS_DEVICE_SYSFS_TIMEOUT,
						   error))
		return FALSE;

	/* poll the status */
	return fu_device_retry_full(device,
				    fu_intel_cvs_device_check_status_cb,
				    (self->max_flash_time + self->max_flash_time) *
					self->max_retry_count / 1000,
				    1000, /* ms */
				    progress,
				    error);
}

static gboolean
fu_intel_cvs_device_set_quirk_kv(FuDevice *device,
				 const gchar *key,
				 const gchar *value,
				 GError **error)
{
	FuIntelCvsDevice *self = FU_INTEL_CVS_DEVICE(device);

	/* all optional */
	if (g_strcmp0(key, "IntelCvsMaxDownloadTime") == 0) {
		guint64 tmp = 0;
		if (!fu_strtoull(value, &tmp, 0, G_MAXUINT32, FU_INTEGER_BASE_AUTO, error))
			return FALSE;
		self->max_download_time = tmp;
		return TRUE;
	}
	if (g_strcmp0(key, "IntelCvsMaxFlashTime") == 0) {
		guint64 tmp = 0;
		if (!fu_strtoull(value, &tmp, 0, G_MAXUINT32, FU_INTEGER_BASE_AUTO, error))
			return FALSE;
		self->max_flash_time = tmp;
		return TRUE;
	}
	if (g_strcmp0(key, "IntelCvsMaxRetryCount") == 0) {
		guint64 tmp = 0;
		if (!fu_strtoull(value, &tmp, 0, G_MAXUINT32, FU_INTEGER_BASE_AUTO, error))
			return FALSE;
		self->max_retry_count = tmp;
		return TRUE;
	}

	/* failed */
	g_set_error_literal(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_NOT_SUPPORTED,
			    "quirk key not supported");
	return FALSE;
}

static void
fu_intel_cvs_device_set_progress(FuDevice *self, FuProgress *progress)
{
	fu_progress_set_id(progress, G_STRLOC);
	fu_progress_add_flag(progress, FU_PROGRESS_FLAG_GUESSED);
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_RESTART, 0, "detach");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_WRITE, 57, "write");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_RESTART, 0, "attach");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_BUSY, 43, "reload");
}

static void
fu_intel_cvs_device_init(FuIntelCvsDevice *self)
{
	self->max_download_time = 200000;
	self->max_flash_time = 200000;
	self->max_retry_count = 5;
	fu_device_set_version_format(FU_DEVICE(self), FWUPD_VERSION_FORMAT_QUAD);
	fu_device_set_remove_delay(FU_DEVICE(self), FU_DEVICE_REMOVE_DELAY_RE_ENUMERATE);
	fu_device_add_protocol(FU_DEVICE(self), "com.intel.cvs");
	fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_UPDATABLE);
	fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_SIGNED_PAYLOAD);
	fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_DUAL_IMAGE);
	fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_REQUIRE_AC);
	fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_SELF_RECOVERY);
	fu_device_add_private_flag(FU_DEVICE(self), FU_DEVICE_PRIVATE_FLAG_ONLY_WAIT_FOR_REPLUG);
	fu_device_add_icon(FU_DEVICE(self), "camera-video");
	g_signal_connect(FU_DEVICE(self),
			 "notify::vid",
			 G_CALLBACK(fu_intel_cvs_device_vid_notify_cb),
			 NULL);
	g_signal_connect(FU_DEVICE(self),
			 "notify::pid",
			 G_CALLBACK(fu_intel_cvs_device_pid_notify_cb),
			 NULL);
}

static void
fu_intel_cvs_device_class_init(FuIntelCvsDeviceClass *klass)
{
	FuDeviceClass *device_class = FU_DEVICE_CLASS(klass);
	device_class->to_string = fu_intel_cvs_device_to_string;
	device_class->setup = fu_intel_cvs_device_setup;
	device_class->prepare_firmware = fu_intel_cvs_device_prepare_firmware;
	device_class->write_firmware = fu_intel_cvs_device_write_firmware;
	device_class->set_quirk_kv = fu_intel_cvs_device_set_quirk_kv;
	device_class->set_progress = fu_intel_cvs_device_set_progress;
}
