/*
 * Copyright 2024 Randy Lai <randy.lai@weidahitech.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include <fcntl.h>
#include <linux/hidraw.h>
#include <linux/input.h>

#include "fu-weida-raw-common.h"
#include "fu-weida-raw-device.h"
#include "fu-weida-raw-firmware.h"
#include "fu-weida-raw-struct.h"

#define FU_WEIDA_RAW_DEVICE_IOCTL_TIMEOUT 5000 /* ms */
#define FU_WEIDA_RAW_REQ_DEV_INFO	  0xF2
#define FU_WEIDA_RAW_PAGE_SIZE		  0x1000
#define FU_WEIDA_RAW_FLASH_PAGE_SIZE	  256
#define FU_WEIDA_RAW_USB_MAX_PAYLOAD_SIZE 63

struct _FuWeidaRawDevice {
	FuUdevDevice parent_instance;
	gint32 dev_type;
	gint32 firmware_id;
	gint32 hardware_id;
	gint32 serial_number;
	guint8 firmware_rev_ext;
};

G_DEFINE_TYPE(FuWeidaRawDevice, fu_weida_raw_device, FU_TYPE_UDEV_DEVICE)

static gboolean
fu_weida_raw_device_set_feature(FuWeidaRawDevice *self,
				const guint8 *buf,
				guint bufsz,
				GError **error)
{
	fu_dump_raw(G_LOG_DOMAIN, "SetFeature", buf, bufsz);
	return fu_udev_device_ioctl(FU_UDEV_DEVICE(self),
				    HIDIOCSFEATURE(bufsz),
				    (guint8 *)buf,
				    bufsz,
				    NULL,
				    FU_WEIDA_RAW_DEVICE_IOCTL_TIMEOUT,
				    FU_UDEV_DEVICE_IOCTL_FLAG_NONE,
				    error);
}

static gboolean
fu_weida_raw_device_get_feature(FuWeidaRawDevice *self, guint8 *buf, guint bufsz, GError **error)
{
	if (!fu_udev_device_ioctl(FU_UDEV_DEVICE(self),
				  HIDIOCGFEATURE(bufsz),
				  buf,
				  bufsz,
				  NULL,
				  FU_WEIDA_RAW_DEVICE_IOCTL_TIMEOUT,
				  FU_UDEV_DEVICE_IOCTL_FLAG_NONE,
				  error))
		return FALSE;
	fu_dump_raw(G_LOG_DOMAIN, "GetFeature", buf, bufsz);
	return TRUE;
}

static void
fu_weida_raw_device_to_string(FuDevice *device, guint idt, GString *str)
{
	FuWeidaRawDevice *self = FU_WEIDA_RAW_DEVICE(device);
	fwupd_codec_string_append_hex(str, idt, "DevType", self->dev_type);
	fwupd_codec_string_append_hex(str, idt, "FirmwareId", self->firmware_id);
	fwupd_codec_string_append_hex(str, idt, "HardwareId", self->hardware_id);
	fwupd_codec_string_append_hex(str, idt, "FirmwareRevExt", self->firmware_rev_ext);
}

static gboolean
fu_weida_raw_device_detach(FuDevice *device, FuProgress *progress, GError **error)
{
	// TODO: does the device need to be told to go into update mode?
	return TRUE;
}

static gboolean
fu_weida_raw_device_attach(FuDevice *device, FuProgress *progress, GError **error)
{
	// TODO: does the device need to be told to go into runtime mode or reboot?
	return TRUE;
}

static gboolean
fu_weida_raw_device_reload(FuDevice *device, GError **error)
{
	FuWeidaRawDevice *self = FU_WEIDA_RAW_DEVICE(device);
	/* TODO: reprobe the hardware, or delete this vfunc to use ->setup() as a fallback */
	g_assert(self != NULL);
	return TRUE;
}

static gboolean
fu_weida_raw_device_probe(FuDevice *device, GError **error)
{
	return fu_udev_device_set_physical_id(FU_UDEV_DEVICE(device), "hid", error);
}

static void
fu_weida_raw_check_firmware_id(FuWeidaRawDevice *self)
{
	self->dev_type = FU_WEIDA_RAW_DEV_TYPE_FW_NOT_SUPPORT;

	if ((self->firmware_id & 0xFF000000) == 0x51000000) {
		fu_device_set_summary(FU_DEVICE(self), "SR3.0 touchscreen controller");
		return;
	}
	if ((self->firmware_id & 0xFF000000) == 0x50000000) {
		fu_device_set_summary(FU_DEVICE(self), "CI5.0 touchscreen controller");
		self->dev_type = FU_WEIDA_RAW_DEV_TYPE_FW8790;
		return;
	}
	if ((self->firmware_id & 0xF0000000) == 0x40000000) {
		fu_device_set_summary(FU_DEVICE(self), "CI4.0 or TM4.0 touchscreen controller");
		self->dev_type = FU_WEIDA_RAW_DEV_TYPE_FW8760;
		return;
	}
	if ((self->firmware_id & 0xF0000000) == 0x30000000) {
		fu_device_set_summary(FU_DEVICE(self), "CI3.0 or SR2.0 touchscreen controller");
		self->dev_type = FU_WEIDA_RAW_DEV_TYPE_FW8755;
		return;
	}
	if ((self->firmware_id & 0xFFFF0000) == 0xFFFF0000) {
		fu_device_set_summary(FU_DEVICE(self), "CI3.0 or SR2.0 touchscreen controller");
		self->dev_type = FU_WEIDA_RAW_DEV_TYPE_FW8755;
		return;
	}
}

static gboolean
fu_weida_raw_device_ensure_status(FuWeidaRawDevice *self, GError **error)
{
	guint8 buf[] = {[0] = FU_WEIDA_RAW_REQ_DEV_INFO, [1 ... 63] = 0xff};
	g_autofree gchar *serial = NULL;

	if (!fu_weida_raw_device_get_feature(self, buf, sizeof(buf), error))
		return FALSE;
	self->firmware_id = fu_memread_uint32(&buf[1], G_LITTLE_ENDIAN);
	self->hardware_id = fu_memread_uint32(&buf[5], G_LITTLE_ENDIAN);
	self->serial_number = fu_memread_uint32(&buf[9], G_LITTLE_ENDIAN);
	fu_weida_raw_check_firmware_id(self);
	if (self->firmware_id == 0)
		self->dev_type = self->dev_type | FU_WEIDA_RAW_DEV_TYPE_FW_MAYBE_ISP;
	self->firmware_rev_ext = 0;
	if (self->dev_type == FU_WEIDA_RAW_DEV_TYPE_FW8760)
		self->firmware_rev_ext = buf[33];
	else if (self->dev_type == FU_WEIDA_RAW_DEV_TYPE_FW8790)
		self->firmware_rev_ext = buf[14];

	if (self->dev_type == FU_WEIDA_RAW_DEV_TYPE_FW8755) {
		fu_device_set_version_raw(FU_DEVICE(self), self->firmware_id);
	} else {
		fu_device_set_version_raw(FU_DEVICE(self),
					  ((self->firmware_id & 0x0FFF) << 4) |
					      (self->firmware_rev_ext & 0x000F));
	}
	serial = fu_version_from_uint32(self->serial_number, FWUPD_VERSION_FORMAT_HEX);
	fu_device_set_serial(FU_DEVICE(self), serial);

	/* success */
	return TRUE;
}

static gboolean
fu_weida_raw_device_setup(FuDevice *device, GError **error)
{
	FuWeidaRawDevice *self = FU_WEIDA_RAW_DEVICE(device);

	if (!fu_weida_raw_device_ensure_status(self, error))
		return FALSE;

	/* success */
	return TRUE;
}

static gboolean
fu_weida_raw_w8760_set_device_mode(FuWeidaRawDevice *self, guint8 mode, GError **error)
{
	g_autoptr(FuWeidaRawCmdSetDeviceMode) st = fu_weida_raw_cmd_set_device_mode_new();
	fu_weida_raw_cmd_set_device_mode_set_mode(st, mode);
	return fu_weida_raw_device_set_feature(self, st->data, st->len, error);
}

static gboolean
fu_weida_raw_w8760_command_read(FuWeidaRawDevice *self,
				guint8 *cmd,
				gsize cmdsz,
				guint8 *data,
				gsize datasz,
				GError **error)
{
	g_autoptr(GByteArray) buf = g_byte_array_new();

	if (datasz > 10)
		fu_byte_array_set_size(buf, 64, 0x0);
	else
		fu_byte_array_set_size(buf, 10, 0x0);
	if (!fu_memcpy_safe(buf->data, /* dst */
			    buf->len,
			    0,
			    cmd, /* src */
			    cmdsz,
			    0,
			    cmdsz,
			    error))
		return FALSE;
	if (!fu_weida_raw_device_set_feature(self, buf->data, buf->len, error))
		return FALSE;

	if (buf->len == 64)
		buf->data[0] = FU_WEIDA_RAW_CMD8760_COMMAND63;
	else
		buf->data[0] = FU_WEIDA_RAW_CMD8760_COMMAND9;
	if (!fu_weida_raw_device_get_feature(self, buf->data, buf->len, error))
		return FALSE;

	/* success */
	return fu_memcpy_safe(data, /* dst */
			      datasz,
			      0x0,
			      buf->data, /* src */
			      buf->len,
			      0x1,
			      buf->len - 1,
			      error);
}

static gboolean
fu_weida_raw_w8760_get_status(FuWeidaRawDevice *self,
			      guint8 *buf,
			      guint8 bufsz,
			      guint8 offset,
			      GError **error)
{
	g_autoptr(FuWeidaRawCmdGetDeviceStatus) st = fu_weida_raw_cmd_get_device_status_new();
	fu_weida_raw_cmd_get_device_status_set_offset(st, offset);
	fu_weida_raw_cmd_get_device_status_set_bufsz(st, bufsz);
	return fu_weida_raw_w8760_command_read(self, st->data, st->len, buf, bufsz, error);
}

static gboolean
fu_weida_raw_w8760_get_device_mode(FuWeidaRawDevice *self, guint8 *device_mode, GError **error)
{
	guint8 buf[10] = {0};
	if (!fu_weida_raw_w8760_get_status(self, buf, sizeof(buf), 4, error))
		return FALSE;
	*device_mode = buf[0];
	return TRUE;
}

static gboolean
fu_weida_raw_w8760_protect_flash(FuWeidaRawDevice *self, guint16 protect_mask, GError **error)
{
	g_autoptr(FuWeidaRawCmdProtectFlash) st = fu_weida_raw_cmd_protect_flash_new();
	fu_weida_raw_cmd_protect_flash_set_mask(st, protect_mask);
	fu_weida_raw_cmd_protect_flash_set_mask_inv(st, ~protect_mask);
	return fu_weida_raw_device_set_feature(self, st->data, st->len, error);
}

typedef struct {
	guint8 cmd;
} FuWeidaRawDeviceReq;

static gboolean
fu_weida_w8760_set_n_check_device_mode_cb(FuDevice *device, gpointer user_data, GError **error)
{
	FuWeidaRawDevice *self = FU_WEIDA_RAW_DEVICE(device);
	FuWeidaRawDeviceReq *req = (FuWeidaRawDeviceReq *)user_data;
	guint8 device_mode = 0;

	if (!fu_weida_raw_w8760_set_device_mode(self, req->cmd, error))
		return FALSE;
	fu_device_sleep(device, 30);
	if (!fu_weida_raw_w8760_get_device_mode(self, &device_mode, error))
		return FALSE;
	if (device_mode != req->cmd) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INVALID_DATA,
			    "device is not ready yet");
		return FALSE;
	}

	/* success */
	return TRUE;
}

static gboolean
fu_weida_w8760_wait_cmd_end_cb(FuDevice *device, gpointer user_data, GError **error)
{
	FuWeidaRawDevice *self = FU_WEIDA_RAW_DEVICE(device);
	guint8 buf[10] = {0};

	if (fu_weida_raw_w8760_get_status(self, buf, sizeof(buf), 0, error) <= 0) {
		g_prefix_error(error, "failed to wait-cmd-end: ");
		return FALSE;
	}
	if ((buf[0] & 0x01) != 0) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INVALID_DATA,
			    "device is not ready yet");
		return FALSE;
	}

	/* success */
	return TRUE;
}

static gboolean
fu_weida_raw_w8760_erase_flash(FuWeidaRawDevice *self,
			       guint32 address,
			       guint32 size,
			       GError **error)
{
	g_autoptr(FuWeidaRawCmdEraseFlash) st = fu_weida_raw_cmd_erase_flash_new();
	fu_weida_raw_cmd_erase_flash_set_address_hi(st, address >> 12);
	fu_weida_raw_cmd_erase_flash_set_address_lo(st, ((address & 0x0FFF) + size + 4095) >> 12);
	if (!fu_weida_raw_device_set_feature(self, st->data, st->len, error))
		return FALSE;
	if (!fu_device_retry_full(FU_DEVICE(self),
				  fu_weida_w8760_wait_cmd_end_cb,
				  200,
				  30,
				  NULL,
				  error)) {
		g_prefix_error(error, "failed to erase: ");
		return FALSE;
	}
	return TRUE;
}

static gboolean
fu_weida_raw_w8760_set_flash_address(FuWeidaRawDevice *self, guint32 address, GError **error)
{
	g_autoptr(FuWeidaRawCmdSetFlashAddress) st = fu_weida_raw_cmd_set_flash_address_new();
	fu_weida_raw_cmd_set_flash_address_set_address(st, address);
	return fu_weida_raw_device_set_feature(self, st->data, st->len, error);
}

static gboolean
fu_weida_raw_w8760_flash_write_chunk(FuWeidaRawDevice *self, FuChunk *chk, GError **error)
{
	g_autoptr(FuWeidaRawCmdWriteFlash) st = fu_weida_raw_cmd_write_flash_new();

	/* no point */
	if (fu_weida_raw_block_is_empty(fu_chunk_get_data(chk), fu_chunk_get_data_sz(chk))) {
		g_debug("already empty, no need to write: 0x%x",
			(guint32)fu_chunk_get_address(chk));
		return TRUE;
	}

	/* ensure address is set */
	if (!fu_weida_raw_w8760_set_flash_address(self, fu_chunk_get_address(chk), error))
		return FALSE;

	/* write flash */
	fu_weida_raw_cmd_write_flash_set_size(st, fu_chunk_get_data_sz(chk));
	g_byte_array_append(st, fu_chunk_get_data(chk), fu_chunk_get_data_sz(chk));
	if (!fu_weida_raw_device_set_feature(self, st->data, st->len, error))
		return FALSE;
	if (!fu_device_retry_full(FU_DEVICE(self),
				  fu_weida_w8760_wait_cmd_end_cb,
				  200,
				  30,
				  NULL,
				  error)) {
		g_prefix_error(error, "failed to write chunk %u: ", fu_chunk_get_idx(chk));
		return FALSE;
	}

	/* success */
	return TRUE;
}

static gboolean
fu_weida_raw_w8760_read_buf_response(FuWeidaRawDevice *self,
				     guint8 *buf,
				     gsize bufsz,
				     GError **error)
{
	g_autoptr(FuWeidaRawCmdReadBufferedResponse) st =
	    fu_weida_raw_cmd_read_buffered_response_new();
	fu_weida_raw_cmd_read_buffered_response_set_size(st, bufsz);
	return fu_weida_raw_w8760_command_read(self, st->data, st->len, buf, bufsz, error);
}

static gboolean
fu_weida_raw_w8760_checksum_flash(FuWeidaRawDevice *self,
				  guint16 *pchksum,
				  guint32 flash_address,
				  guint32 size,
				  GError **error)
{
	guint8 buf[10] = {0};
	g_autoptr(FuWeidaRawCmdCalculateFlashChecksum) st =
	    fu_weida_raw_cmd_calculate_flash_checksum_new();

	fu_weida_raw_cmd_calculate_flash_checksum_set_flash_address(st, flash_address);
	fu_weida_raw_cmd_calculate_flash_checksum_set_size(st, size);
	if (!fu_weida_raw_device_set_feature(self, st->data, st->len, error))
		return FALSE;
	if (!fu_device_retry_full(FU_DEVICE(self),
				  fu_weida_w8760_wait_cmd_end_cb,
				  200,
				  30,
				  NULL,
				  error))
		return FALSE;
	if (fu_weida_raw_w8760_read_buf_response(self, buf, 10, error) <= 0)
		return FALSE;

	*pchksum = fu_memread_uint16(buf, G_LITTLE_ENDIAN);
	return TRUE;
}

static gboolean
fu_weida_raw_w8760_flash_write_data(FuWeidaRawDevice *self,
				    guint32 address,
				    GBytes *blob,
				    GError **error)
{
	g_autoptr(GPtrArray) chunks = NULL;

	if ((address & 0x3) != 0) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_NOT_SUPPORTED,
				    "address alignment bad");
		return FALSE;
	}

	chunks = fu_chunk_array_new(g_bytes_get_data(blob, NULL),
				    g_bytes_get_size(blob),
				    address,
				    0,
				    FU_WEIDA_RAW_USB_MAX_PAYLOAD_SIZE - 2);

	for (guint i = 0; i < chunks->len; i++) {
		FuChunk *chk = g_ptr_array_index(chunks, i);
		g_debug("address: 0x%x", (guint32)fu_chunk_get_address(chk));

		g_debug("data size 0x%x", fu_chunk_get_data_sz(chk));

		if (chk == NULL)
			return FALSE;
		if (!fu_weida_raw_w8760_flash_write_chunk(self, chk, error))
			return FALSE;
	}

	/* success */
	return TRUE;
}

static gboolean
fu_weida_raw_w8760_dev_reboot(FuWeidaRawDevice *self, GError **error)
{
	g_autoptr(FuWeidaRawCmdReboot) st = fu_weida_raw_cmd_reboot_new();
	return fu_weida_raw_device_set_feature(self, st->data, st->len, error);
}

static gboolean
fu_weida_raw_device_writeln(const gchar *fn, const gchar *buf, GError **error)
{
	int fd;
	g_autoptr(FuIOChannel) io = NULL;

	fd = open(fn, O_WRONLY);
	if (fd < 0) {
		g_set_error(error, FWUPD_ERROR, FWUPD_ERROR_INVALID_FILE, "could not open %s", fn);
		return FALSE;
	}
	// FIXME: use fu_io_channel_new_file() after rebasing to get the new flags
	io = fu_io_channel_unix_new(fd);
	return fu_io_channel_write_raw(io,
				       (const guint8 *)buf,
				       strlen(buf),
				       1000,
				       FU_IO_CHANNEL_FLAG_NONE,
				       error);
}

static gboolean
fu_wdt_hid_device_rebind_driver(FuWeidaRawDevice *self, GError **error)
{
	GUdevDevice *udev_device = fu_udev_device_get_dev(FU_UDEV_DEVICE(self));
	const gchar *hid_id;
	const gchar *driver;
	const gchar *subsystem;
	g_autofree gchar *fn_rebind = NULL;
	g_autofree gchar *fn_unbind = NULL;
	g_autoptr(GUdevDevice) parent_hid = NULL;
	g_autoptr(GUdevDevice) parent_phys = NULL;
	g_auto(GStrv) hid_strs = NULL;

	/* get actual HID node */
	parent_hid = g_udev_device_get_parent_with_subsystem(udev_device, "hid", NULL);
	if (parent_hid == NULL) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INVALID_FILE,
			    "no HID parent device for %s",
			    g_udev_device_get_sysfs_path(udev_device));
		return FALSE;
	}

	/* build paths */
	parent_phys = g_udev_device_get_parent_with_subsystem(udev_device, "i2c", NULL);
	if (parent_phys == NULL) {
		parent_phys = g_udev_device_get_parent_with_subsystem(udev_device, "usb", NULL);
		if (parent_phys == NULL) {
			g_set_error(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INVALID_FILE,
				    "no parent device for %s",
				    g_udev_device_get_sysfs_path(parent_hid));
			return FALSE;
		}
	}

	/* find the physical ID to use for the rebind */
	hid_strs = g_strsplit(g_udev_device_get_sysfs_path(parent_phys), "/", -1);
	hid_id = hid_strs[g_strv_length(hid_strs) - 1];
	if (hid_id == NULL) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INVALID_FILE,
			    "no HID_PHYS in %s",
			    g_udev_device_get_sysfs_path(parent_phys));
		return FALSE;
	}

	driver = g_udev_device_get_driver(parent_phys);
	subsystem = g_udev_device_get_subsystem(parent_phys);
	fn_rebind = g_build_filename("/sys/bus/", subsystem, "drivers", driver, "bind", NULL);
	fn_unbind = g_build_filename("/sys/bus/", subsystem, "drivers", driver, "unbind", NULL);

	/* unbind hidraw, then bind it again to get a replug */
	fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_WAIT_FOR_REPLUG);
	if (!fu_weida_raw_device_writeln(fn_unbind, hid_id, error))
		return FALSE;
	if (!fu_weida_raw_device_writeln(fn_rebind, hid_id, error))
		return FALSE;

	/* success */
	return TRUE;
}

static gboolean
fu_weida_raw_w8760_write_image_cb(FuDevice *device, gpointer user_data, GError **error)
{
	FuWeidaRawDevice *self = FU_WEIDA_RAW_DEVICE(device);
	FuChunk *chk = FU_CHUNK(user_data);
	guint16 calc_checksum;
	guint16 read_checksum = 0;
	g_autoptr(GBytes) blob = fu_chunk_get_bytes(chk);

	if (!fu_weida_raw_w8760_flash_write_data(self, fu_chunk_get_address(chk), blob, error))
		return FALSE;
	calc_checksum = fu_misr16(0, fu_chunk_get_data(chk), fu_chunk_get_data_sz(chk));
	if (!fu_weida_raw_w8760_checksum_flash(self,
					       &read_checksum,
					       fu_chunk_get_address(chk),
					       fu_chunk_get_data_sz(chk),
					       error))
		return FALSE;
	if (read_checksum != calc_checksum) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INVALID_DATA,
			    "checksum failed (%u): 0x%x != 0x%x",
			    fu_chunk_get_idx(chk),
			    read_checksum,
			    calc_checksum);
		return FALSE;
	}

	/* success */
	return TRUE;
}

static gboolean
fu_weida_raw_w8760_write_image(FuWeidaRawDevice *self,
			       gsize address,
			       GInputStream *stream,
			       FuProgress *progress,
			       GError **error)
{
	gsize bufsz = 0;
	g_autoptr(FuChunkArray) chunks = NULL;

	if (!fu_input_stream_size(stream, &bufsz, error))
		return FALSE;

	if (!fu_weida_raw_w8760_erase_flash(self, address, bufsz, error)) {
		g_prefix_error(error, "erase flash failed: ");
		return FALSE;
	}

	chunks = fu_chunk_array_new_from_stream(stream, address, FU_WEIDA_RAW_PAGE_SIZE, error);
	if (chunks == NULL)
		return FALSE;
	fu_progress_set_id(progress, G_STRLOC);
	fu_progress_set_steps(progress, fu_chunk_array_length(chunks));
	for (guint i = 0; i < fu_chunk_array_length(chunks); i++) {
		g_autoptr(FuChunk) chk = NULL;
		chk = fu_chunk_array_index(chunks, i, error);
		if (chk == NULL)
			return FALSE;
		if (!fu_device_retry(FU_DEVICE(self),
				     fu_weida_raw_w8760_write_image_cb,
				     5,
				     chk,
				     error))
			return FALSE;
		fu_progress_step_done(progress);
	}

	/* success */
	return TRUE;
}

static gboolean
fu_weida_raw_w8760_write_images(FuWeidaRawDevice *self,
				FuFirmware *firmware,
				FuProgress *progress,
				GError **error)
{
	g_autoptr(GPtrArray) imgs = fu_firmware_get_images(firmware);

	fu_progress_set_id(progress, G_STRLOC);
	fu_progress_set_steps(progress, imgs->len);
	for (guint i = 0; i < imgs->len; i++) {
		FuFirmware *img = g_ptr_array_index(imgs, i);
		g_autoptr(GInputStream) stream = NULL;

		stream = fu_firmware_get_stream(img, error);
		if (stream == NULL)
			return FALSE;
		if (!fu_weida_raw_w8760_write_image(self,
						    fu_firmware_get_addr(img),
						    stream,
						    fu_progress_get_child(progress),
						    error))
			return FALSE;
		fu_progress_step_done(progress);
	}

	/* success */
	return TRUE;
}

static gboolean
fu_weida_raw_w8760_write_wif1(FuWeidaRawDevice *self,
			      FuFirmware *firmware,
			      FuProgress *progress,
			      GError **error)
{
	FuWeidaRawDeviceReq req = {.cmd = FU_WEIDA_RAW_CMD8760_MODE_FLASH_PROGRAM};

	/* progress */
	fu_progress_set_id(progress, G_STRLOC);
	fu_progress_add_flag(progress, FU_PROGRESS_FLAG_GUESSED);
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_WRITE, 5, "check-mode");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_WRITE, 5, "protect-flash");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_WRITE, 90, "write-images");

	if (!fu_device_retry(FU_DEVICE(self),
			     fu_weida_w8760_set_n_check_device_mode_cb,
			     20,
			     &req,
			     error)) {
		g_prefix_error(error, "failed to set device to flash program mode ");
		return FALSE;
	}
	fu_progress_step_done(progress);

	if (!fu_weida_raw_w8760_protect_flash(self,
					      FU_WEIDA_RAW_CMD8760U16_UNPROTECT_LOWER508K,
					      error)) {
		g_prefix_error(error, "W8760_UnprotectLower508k failed: ");
		return FALSE;
	}
	fu_progress_step_done(progress);

	if (!fu_weida_raw_w8760_write_images(self,
					     firmware,
					     fu_progress_get_child(progress),
					     error))
		return FALSE;
	fu_progress_step_done(progress);

	/* success */
	return TRUE;
}

static gboolean
fu_weida_raw_device_prepare(FuDevice *device,
			    FuProgress *progress,
			    FwupdInstallFlags flags,
			    GError **error)
{
	FuWeidaRawDevice *self = FU_WEIDA_RAW_DEVICE(device);
	/* TODO: anything the device has to do before the update starts */
	g_assert(self != NULL);
	return TRUE;
}

static gboolean
fu_weida_raw_device_cleanup(FuDevice *device,
			    FuProgress *progress,
			    FwupdInstallFlags flags,
			    GError **error)
{
	FuWeidaRawDevice *self = FU_WEIDA_RAW_DEVICE(device);
	if (!fu_weida_raw_w8760_protect_flash(self, FU_WEIDA_RAW_CMD8760U16_PROTECT_ALL, error)) {
		g_prefix_error(error, "protect all failed: ");
		return FALSE;
	}
	return TRUE;
}

static gboolean
fu_weida_raw_device_write_firmware(FuDevice *device,
				   FuFirmware *firmware,
				   FuProgress *progress,
				   FwupdInstallFlags flags,
				   GError **error)
{
	FuWeidaRawDevice *self = FU_WEIDA_RAW_DEVICE(device);

	/* sanity check: add other weida devices as required */
	if (self->dev_type != FU_WEIDA_RAW_DEV_TYPE_FW8760) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_NOT_SUPPORTED,
			    "device type 0x%x not supported",
			    (guint)self->dev_type);
		return FALSE;
	}

	/* progress */
	fu_progress_set_id(progress, G_STRLOC);
	fu_progress_add_flag(progress, FU_PROGRESS_FLAG_GUESSED);
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_WRITE, 44, NULL);
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_VERIFY, 35, NULL);

	/* get default image */
	if (!fu_weida_raw_w8760_write_wif1(self, firmware, fu_progress_get_child(progress), error))
		return FALSE;

	// FIXME: can this move to detach() or cleanup()?
	if (!fu_weida_raw_w8760_dev_reboot(self, error))
		return FALSE;
	fu_progress_step_done(progress);

	fu_device_sleep(device, 2 * 1000);

	if (!fu_device_has_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_INTERNAL)) {
		fu_device_set_remove_delay(FU_DEVICE(self), FU_DEVICE_REMOVE_DELAY_RE_ENUMERATE);
		fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_WAIT_FOR_REPLUG);
	} else {
		fu_wdt_hid_device_rebind_driver(self, error);
	}

	/* TODO: verify each block */
	fu_progress_step_done(progress);

	/* success! */
	return TRUE;
}

static gboolean
fu_weida_raw_device_set_quirk_kv(FuDevice *device,
				 const gchar *key,
				 const gchar *value,
				 GError **error)
{
	/* TODO: parse value from quirk file */
	return TRUE;
	/* failed */
	g_set_error_literal(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_NOT_SUPPORTED,
			    "quirk key not supported");
	return FALSE;
}

static void
fu_weida_raw_device_set_progress(FuDevice *self, FuProgress *progress)
{
	fu_progress_set_id(progress, G_STRLOC);
	fu_progress_add_flag(progress, FU_PROGRESS_FLAG_GUESSED);
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_RESTART, 0, "detach");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_WRITE, 57, "write");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_RESTART, 0, "attach");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_BUSY, 43, "reload");
}

static gchar *
fu_weida_raw_device_convert_version(FuDevice *device, guint64 version_raw)
{
	return fu_version_from_uint32(version_raw, fu_device_get_version_format(device));
}

static void
fu_weida_raw_device_init(FuWeidaRawDevice *self)
{
	fu_device_set_version_format(FU_DEVICE(self), FWUPD_VERSION_FORMAT_HEX);
	fu_device_set_remove_delay(FU_DEVICE(self), FU_DEVICE_REMOVE_DELAY_RE_ENUMERATE);
	fu_device_add_protocol(FU_DEVICE(self), "com.weida.raw");
	fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_UPDATABLE);
	fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_UNSIGNED_PAYLOAD);
	fu_device_add_internal_flag(FU_DEVICE(self), FU_DEVICE_INTERNAL_FLAG_ONLY_WAIT_FOR_REPLUG);
	fu_device_add_icon(FU_DEVICE(self), "input-tablet");
	fu_device_set_name(FU_DEVICE(self), "CoolTouch Device");
	fu_device_set_vendor(FU_DEVICE(self), "WEIDA");
	fu_device_set_firmware_gtype(FU_DEVICE(self), FU_TYPE_WEIDA_RAW_FIRMWARE);

	fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_REQUIRE_AC);
	if (FU_DEVICE(self) != NULL) {
		const gchar *back_id = fu_device_get_backend_id(FU_DEVICE(self));
		if (back_id != NULL) {
			const gchar *i2c_id =
			    g_strrstr(fu_device_get_backend_id(FU_DEVICE(self)), "i2c");
			if (i2c_id != NULL)
				fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_INTERNAL);
		}
	}
	fu_udev_device_add_open_flag(FU_UDEV_DEVICE(self), FU_IO_CHANNEL_OPEN_FLAG_READ);
	fu_udev_device_add_open_flag(FU_UDEV_DEVICE(self), FU_IO_CHANNEL_OPEN_FLAG_WRITE);
	fu_udev_device_add_open_flag(FU_UDEV_DEVICE(self), FU_IO_CHANNEL_OPEN_FLAG_NONBLOCK);
}

static void
fu_weida_raw_device_finalize(GObject *object)
{
	FuWeidaRawDevice *self = FU_WEIDA_RAW_DEVICE(object);

	/* TODO: free any allocated instance state here */
	g_assert(self != NULL);

	G_OBJECT_CLASS(fu_weida_raw_device_parent_class)->finalize(object);
}

static void
fu_weida_raw_device_class_init(FuWeidaRawDeviceClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS(klass);
	FuDeviceClass *device_class = FU_DEVICE_CLASS(klass);
	object_class->finalize = fu_weida_raw_device_finalize;
	device_class->to_string = fu_weida_raw_device_to_string;
	device_class->probe = fu_weida_raw_device_probe;
	device_class->setup = fu_weida_raw_device_setup;
	device_class->reload = fu_weida_raw_device_reload;
	device_class->prepare = fu_weida_raw_device_prepare;
	device_class->cleanup = fu_weida_raw_device_cleanup;
	device_class->attach = fu_weida_raw_device_attach;
	device_class->detach = fu_weida_raw_device_detach;
	device_class->write_firmware = fu_weida_raw_device_write_firmware;
	device_class->set_quirk_kv = fu_weida_raw_device_set_quirk_kv;
	device_class->set_progress = fu_weida_raw_device_set_progress;
	device_class->convert_version = fu_weida_raw_device_convert_version;
}
