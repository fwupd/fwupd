/*
 * Copyright 2024 Mike Chang <Mike.chang@telink-semi.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#ifdef HAVE_HIDRAW_H
#include <linux/hidraw.h>
#include <linux/input.h>
#endif

#include "fu-telink-dfu-archive.h"
#include "fu-telink-dfu-firmware.h"
#include "fu-telink-dfu-hid-device.h"
#include "fu-telink-dfu-struct.h"

/* this can be set using Flags=example in the quirk file  */
#define FU_TELINK_DFU_HID_DEVICE_FLAG_EXAMPLE (1 << 0)

struct _FuTelinkDfuHidDevice {
	FuUdevDevice parent_instance;
	gchar *board_name;
	gchar *bl_name;
	guint16 start_addr;
};

G_DEFINE_TYPE(FuTelinkDfuHidDevice, fu_telink_dfu_hid_device, FU_TYPE_UDEV_DEVICE)

#define FU_TELINK_DFU_HID_DEVICE_RETRY_INTERVAL 50  /* ms */
#define FU_TELINK_DFU_HID_DEVICE_IOCTL_TIMEOUT	500 /* ms */

static void
fu_telink_dfu_hid_device_to_string(FuDevice *device, guint idt, GString *str)
{
	FuTelinkDfuHidDevice *self = FU_TELINK_DFU_HID_DEVICE(device);
	fwupd_codec_string_append(str, idt, "BoardName", self->board_name);
	fwupd_codec_string_append(str, idt, "Bootloader", self->bl_name);
}

static gboolean
fu_telink_dfu_hid_device_get_report(FuTelinkDfuHidDevice *self,
				    guint8 *buf,
				    gsize bufsz,
				    GError **error)
{
#ifdef HAVE_HIDRAW_H
	if (!fu_udev_device_ioctl(FU_UDEV_DEVICE(self),
				  HIDIOCGFEATURE(bufsz),
				  buf,
				  NULL,
				  FU_TELINK_DFU_HID_DEVICE_IOCTL_TIMEOUT,
				  error)) {
		g_prefix_error(error, "failed get report: ");
		return FALSE;
	}
	return TRUE;
#else
	g_set_error_literal(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_NOT_SUPPORTED,
			    "<linux/hidraw.h> not available");
	return FALSE;
#endif
}

static gboolean
fu_telink_dfu_hid_device_set_report(FuTelinkDfuHidDevice *self,
				    guint8 *buf,
				    gsize bufsz,
				    GError **error)
{
#ifdef HAVE_HIDRAW_H
	if (!fu_udev_device_ioctl(FU_UDEV_DEVICE(self),
				  HIDIOCSFEATURE(bufsz),
				  buf,
				  NULL,
				  FU_TELINK_DFU_HID_DEVICE_IOCTL_TIMEOUT,
				  error)) {
		g_prefix_error(error, "failed set report: ");
		return FALSE;
	}
	return TRUE;
#else
	g_set_error_literal(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_NOT_SUPPORTED,
			    "<linux/hidraw.h> not available");
	return FALSE;
#endif
}

static gboolean
fu_telink_dfu_hid_device_detach(FuDevice *device, FuProgress *progress, GError **error)
{
	FuTelinkDfuHidDevice *self = FU_TELINK_DFU_HID_DEVICE(device);

	/* sanity check */
	if (fu_device_has_flag(device, FWUPD_DEVICE_FLAG_IS_BOOTLOADER)) {
		g_debug("already in bootloader mode, skipping");
		return TRUE;
	}

	/* TODO: switch the device into bootloader mode */
	g_assert(self != NULL);

	fu_device_add_flag(device, FWUPD_DEVICE_FLAG_WAIT_FOR_REPLUG);
	return TRUE;
}

static gboolean
fu_telink_dfu_hid_device_attach(FuDevice *device, FuProgress *progress, GError **error)
{
	FuTelinkDfuHidDevice *self = FU_TELINK_DFU_HID_DEVICE(device);

	/* sanity check */
	if (!fu_device_has_flag(device, FWUPD_DEVICE_FLAG_IS_BOOTLOADER)) {
		g_debug("already in runtime mode, skipping");
		return TRUE;
	}

	/* TODO: switch the device into runtime mode */
	g_assert(self != NULL);

	fu_device_add_flag(device, FWUPD_DEVICE_FLAG_WAIT_FOR_REPLUG);
	return TRUE;
}

static gboolean
fu_telink_dfu_hid_device_reload(FuDevice *device, GError **error)
{
	FuTelinkDfuHidDevice *self = FU_TELINK_DFU_HID_DEVICE(device);
	/* TODO: reprobe the hardware, or delete this vfunc to use ->setup() as a fallback */
	g_assert(self != NULL);
	return TRUE;
}

static gboolean
fu_telink_dfu_hid_device_probe(FuDevice *device, GError **error)
{
	return fu_udev_device_set_physical_id(FU_UDEV_DEVICE(device), "hid", error);
}

static gboolean
fu_telink_dfu_hid_device_setup(FuDevice *device, GError **error)
{
	FuTelinkDfuHidDevice *self = FU_TELINK_DFU_HID_DEVICE(device);

	if (0) {
		g_autoptr(FuStructTelinkDfuHidReport) st_hid =
		    fu_struct_telink_dfu_hid_report_new();
		fu_struct_telink_dfu_hid_report_set_report_id(st_hid, 0x99);
		if (!fu_telink_dfu_hid_device_set_report(self, st_hid->data, st_hid->len, error))
			return FALSE;
		if (!fu_telink_dfu_hid_device_get_report(self, st_hid->data, st_hid->len, error))
			return FALSE;
	}

	/* TODO: get the version and other properties from the hardware while open */
	g_assert(self != NULL);
	fu_device_set_version(device, "1.2.3");

	/* success */
	return TRUE;
}

static gboolean
fu_telink_dfu_hid_device_prepare(FuDevice *device,
				 FuProgress *progress,
				 FwupdInstallFlags flags,
				 GError **error)
{
	FuTelinkDfuHidDevice *self = FU_TELINK_DFU_HID_DEVICE(device);
	/* TODO: anything the device has to do before the update starts */
	g_assert(self != NULL);
	return TRUE;
}

static gboolean
fu_telink_dfu_hid_device_cleanup(FuDevice *device,
				 FuProgress *progress,
				 FwupdInstallFlags flags,
				 GError **error)
{
	FuTelinkDfuHidDevice *self = FU_TELINK_DFU_HID_DEVICE(device);
	/* TODO: anything the device has to do when the update has completed */
	g_assert(self != NULL);
	return TRUE;
}

static FuFirmware *
fu_telink_dfu_hid_device_prepare_firmware(FuDevice *device,
					  GInputStream *stream,
					  FuProgress *progress,
					  FwupdInstallFlags flags,
					  GError **error)
{
	FuTelinkDfuHidDevice *self = FU_TELINK_DFU_HID_DEVICE(device);
	g_autoptr(FuFirmware) firmware = fu_telink_dfu_firmware_new();

	/* TODO: you do not need to use this vfunc if not checking attributes */
	if (self->start_addr !=
	    fu_telink_dfu_firmware_get_crc32(FU_TELINK_DFU_FIRMWARE(firmware))) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INVALID_FILE,
			    "start address mismatch, got 0x%04x, expected 0x%04x",
			    fu_telink_dfu_firmware_get_crc32(FU_TELINK_DFU_FIRMWARE(firmware)),
			    self->start_addr);
		return NULL;
	}

	if (!fu_firmware_parse_stream(firmware, stream, 0x0, flags, error))
		return NULL;
	return g_steal_pointer(&firmware);
}

static gboolean
fu_telink_dfu_hid_device_write_blocks(FuTelinkDfuHidDevice *self,
				      FuChunkArray *chunks,
				      FuProgress *progress,
				      GError **error)
{
	/* progress */
	fu_progress_set_id(progress, G_STRLOC);
	fu_progress_set_steps(progress, fu_chunk_array_length(chunks));
	for (guint i = 0; i < fu_chunk_array_length(chunks); i++) {
		g_autoptr(FuChunk) chk = NULL;
		guint8 buf[64] = {0x12, 0x24, 0x0}; /* TODO: this is the preamble */

		/* prepare chunk */
		chk = fu_chunk_array_index(chunks, i, error);
		if (chk == NULL)
			return FALSE;

		/* TODO: send to hardware */
		if (!fu_memcpy_safe(buf,
				    sizeof(buf),
				    0x2, /* TODO: copy to dst at offset */
				    fu_chunk_get_data(chk),
				    fu_chunk_get_data_sz(chk),
				    0x0, /* src */
				    fu_chunk_get_data_sz(chk),
				    error))
			return FALSE;

		/* update progress */
		fu_progress_step_done(progress);
	}

	/* success */
	return TRUE;
}

static gboolean
fu_telink_dfu_hid_device_write_firmware(FuDevice *device,
					FuFirmware *firmware,
					FuProgress *progress,
					FwupdInstallFlags flags,
					GError **error)
{
	FuTelinkDfuHidDevice *self = FU_TELINK_DFU_HID_DEVICE(device);
	g_autoptr(GInputStream) stream = NULL;
	g_autoptr(FuChunkArray) chunks = NULL;

	/* progress */
	fu_progress_set_id(progress, G_STRLOC);
	fu_progress_add_flag(progress, FU_PROGRESS_FLAG_GUESSED);
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_WRITE, 44, NULL);
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_VERIFY, 35, NULL);

	/* get default image */
	stream = fu_firmware_get_stream(firmware, error);
	if (stream == NULL)
		return FALSE;

	/* write each block */
	chunks =
	    fu_chunk_array_new_from_stream(stream, self->start_addr, 64 /* block_size */, error);
	if (chunks == NULL)
		return FALSE;
	if (!fu_telink_dfu_hid_device_write_blocks(self,
						   chunks,
						   fu_progress_get_child(progress),
						   error))
		return FALSE;
	fu_progress_step_done(progress);

	/* TODO: verify each block */
	fu_progress_step_done(progress);

	/* success! */
	return TRUE;
}

static gboolean
fu_telink_dfu_hid_device_set_quirk_kv(FuDevice *device,
				      const gchar *key,
				      const gchar *value,
				      GError **error)
{
	FuTelinkDfuHidDevice *self = FU_TELINK_DFU_HID_DEVICE(device);

	/* parse value from quirk file */
	if (g_strcmp0(key, "TelinkDfuBootType") == 0) {
		if (g_strcmp0(value, "beta") == 0 || g_strcmp0(value, "otav1") == 0) {
			self->bl_name = g_strdup(value);
		} else {
			g_set_error_literal(error,
					    FWUPD_ERROR,
					    FWUPD_ERROR_INVALID_DATA,
					    "bad TelinkDfuBootType");
			return FALSE;
		}
	}
	if (g_strcmp0(key, "TelinkDfuBoardType") == 0) {
		if (g_strcmp0(value, "tlsr8278") == 0 || g_strcmp0(value, "tlsr8208") == 0) {
			self->board_name = g_strdup(value);
		} else {
			g_set_error_literal(error,
					    FWUPD_ERROR,
					    FWUPD_ERROR_INVALID_DATA,
					    "bad TelinkDfuBoardType");
			return FALSE;
		}
	}

	/* failed */
	g_set_error_literal(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_NOT_SUPPORTED,
			    "quirk key not supported");
	return FALSE;
}

static void
fu_telink_dfu_hid_device_set_progress(FuDevice *self, FuProgress *progress)
{
	fu_progress_set_id(progress, G_STRLOC);
	fu_progress_add_flag(progress, FU_PROGRESS_FLAG_GUESSED);
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_RESTART, 0, "detach");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_WRITE, 57, "write");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_RESTART, 0, "attach");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_BUSY, 43, "reload");
}

static void
fu_telink_dfu_hid_device_init(FuTelinkDfuHidDevice *self)
{
	self->start_addr = 0x5000;
	fu_device_set_vendor(FU_DEVICE(self), "Telink");
	fu_device_set_version_format(FU_DEVICE(self), FWUPD_VERSION_FORMAT_TRIPLET);
	fu_device_set_remove_delay(FU_DEVICE(self), FU_DEVICE_REMOVE_DELAY_RE_ENUMERATE);
	fu_device_set_firmware_gtype(FU_DEVICE(self), FU_TYPE_TELINK_DFU_ARCHIVE);
	fu_device_add_protocol(FU_DEVICE(self), "com.telink.dfu");
	fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_UPDATABLE);
	// todo: FWUPD_DEVICE_FLAG_SIGNED_PAYLOAD?
	fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_UNSIGNED_PAYLOAD);
	fu_udev_device_add_flag(FU_UDEV_DEVICE(self), FU_UDEV_DEVICE_FLAG_OPEN_WRITE);
	fu_device_add_internal_flag(FU_DEVICE(self), FU_DEVICE_INTERNAL_FLAG_ONLY_WAIT_FOR_REPLUG);
	fu_device_retry_set_delay(FU_DEVICE(self), FU_TELINK_DFU_HID_DEVICE_RETRY_INTERVAL);
	fu_device_register_private_flag(FU_DEVICE(self),
					FU_TELINK_DFU_HID_DEVICE_FLAG_EXAMPLE,
					"example");
}

static void
fu_telink_dfu_hid_device_finalize(GObject *object)
{
	FuTelinkDfuHidDevice *self = FU_TELINK_DFU_HID_DEVICE(object);
	g_free(self->board_name);
	g_free(self->bl_name);
	G_OBJECT_CLASS(fu_telink_dfu_hid_device_parent_class)->finalize(object);
}

static void
fu_telink_dfu_hid_device_class_init(FuTelinkDfuHidDeviceClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS(klass);
	FuDeviceClass *device_class = FU_DEVICE_CLASS(klass);
	object_class->finalize = fu_telink_dfu_hid_device_finalize;
	device_class->to_string = fu_telink_dfu_hid_device_to_string;
	device_class->probe = fu_telink_dfu_hid_device_probe;
	device_class->setup = fu_telink_dfu_hid_device_setup;
	device_class->reload = fu_telink_dfu_hid_device_reload;
	device_class->prepare = fu_telink_dfu_hid_device_prepare;
	device_class->cleanup = fu_telink_dfu_hid_device_cleanup;
	device_class->attach = fu_telink_dfu_hid_device_attach;
	device_class->detach = fu_telink_dfu_hid_device_detach;
	device_class->prepare_firmware = fu_telink_dfu_hid_device_prepare_firmware;
	device_class->write_firmware = fu_telink_dfu_hid_device_write_firmware;
	device_class->set_quirk_kv = fu_telink_dfu_hid_device_set_quirk_kv;
	device_class->set_progress = fu_telink_dfu_hid_device_set_progress;
}
