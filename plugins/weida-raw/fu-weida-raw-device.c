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

#define FU_WEIDA_RAW_FW_NOT_SUPPORT 0x00
#define FU_WEIDA_RAW_FW_MAYBE_ISP   0x01
#define FU_WEIDA_RAW_FW8755	    0x02
#define FU_WEIDA_RAW_FW8760	    0x04
#define FU_WEIDA_RAW_FW8762	    0x08
#define FU_WEIDA_RAW_FW8790	    0X10

static gboolean
fu_weida_raw_device_set_feature(FuWeidaRawDevice *self,
				const guint8 *data,
				guint datasz,
				GError **error)
{
	fu_dump_raw(G_LOG_DOMAIN, "SetFeature", data, datasz);
	return fu_udev_device_ioctl(FU_UDEV_DEVICE(self),
				    HIDIOCSFEATURE(datasz),
				    (guint8 *)data,
				    NULL,
				    FU_WEIDA_RAW_DEVICE_IOCTL_TIMEOUT,
				    error);
	return TRUE;
}

static gboolean
fu_weida_raw_device_get_feature(FuWeidaRawDevice *self, guint8 *data, guint datasz, GError **error)
{
	if (!fu_udev_device_ioctl(FU_UDEV_DEVICE(self),
				  HIDIOCGFEATURE(datasz),
				  data,
				  NULL,
				  FU_WEIDA_RAW_DEVICE_IOCTL_TIMEOUT,
				  error))
		return FALSE;
	fu_dump_raw(G_LOG_DOMAIN, "GetFeature", data, datasz);
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
	return TRUE;
}

static gboolean
fu_weida_raw_device_attach(FuDevice *device, FuProgress *progress, GError **error)
{
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
fu_weida_raw_check_firmware_id(FuWeidaRawDevice *self, guint32 fwid)
{
	self->dev_type = FU_WEIDA_RAW_FW_NOT_SUPPORT;

	if ((fwid & 0xFF000000) == 0x51000000) {
		fu_device_set_summary(FU_DEVICE(self), "SR3.0 touchscreen controller");
		return;
	}
	if ((fwid & 0xFF000000) == 0x50000000) {
		fu_device_set_summary(FU_DEVICE(self), "CI5.0 touchscreen controller");
		self->dev_type = FU_WEIDA_RAW_FW8790;
		return;
	}
	if ((fwid & 0xF0000000) == 0x40000000) {
		fu_device_set_summary(FU_DEVICE(self), "CI4.0 or TM4.0 touchscreen controller");
		self->dev_type = FU_WEIDA_RAW_FW8760;
		return;
	}
	if ((fwid & 0xF0000000) == 0x30000000) {
		fu_device_set_summary(FU_DEVICE(self), "CI3.0 or SR2.0 touchscreen controller");
		self->dev_type = FU_WEIDA_RAW_FW8755;
		return;
	}
	if ((fwid & 0xFFFF0000) == 0xFFFF0000) {
		fu_device_set_summary(FU_DEVICE(self), "CI3.0 or SR2.0 touchscreen controller");
		self->dev_type = FU_WEIDA_RAW_FW8755;
		return;
	}
}

static gboolean
fu_weida_raw_device_ensure_status(FuWeidaRawDevice *self, GError **error)
{
	guint8 buf[] = {[0] = FU_WEIDA_RAW_REQ_DEV_INFO, [1 ... 63] = 0xff};
	g_autofree gchar *serial = NULL;
	g_autofree gchar *version = NULL;

	if (!fu_weida_raw_device_get_feature(self, buf, sizeof(buf), error))
		return FALSE;
	self->firmware_id = fu_memread_uint32(&buf[1], G_LITTLE_ENDIAN);
	self->hardware_id = fu_memread_uint32(&buf[5], G_LITTLE_ENDIAN);
	self->serial_number = fu_memread_uint32(&buf[9], G_LITTLE_ENDIAN);
	fu_weida_raw_check_firmware_id(self, self->firmware_id);
	if (self->firmware_id == 0)
		self->dev_type = self->dev_type | FU_WEIDA_RAW_FW_MAYBE_ISP;
	self->firmware_rev_ext = 0;
	if (self->dev_type == FU_WEIDA_RAW_FW8760)
		self->firmware_rev_ext = buf[33];
	else if (self->dev_type == FU_WEIDA_RAW_FW8790)
		self->firmware_rev_ext = buf[14];

	if (self->dev_type == FU_WEIDA_RAW_FW8755) {
		version = fu_version_from_uint16(self->firmware_id, FWUPD_VERSION_FORMAT_HEX);
	} else {
		version = fu_version_from_uint16(((self->firmware_id & 0x0FFF) << 4) |
						     (self->firmware_rev_ext & 0x000F),
						 FWUPD_VERSION_FORMAT_HEX);
	}
	fu_device_set_version(FU_DEVICE(self), version);
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
fu_weida_raw_w8760_command_write(FuWeidaRawDevice *self,
				 guint8 *data,
				 int start,
				 int size,
				 GError **error)
{
	return fu_weida_raw_device_set_feature(self, &data[start], size, error);
}

static gboolean
fu_weida_raw_w8760_set_device_mode(FuWeidaRawDevice *self, guint8 mode, GError **error)
{
	guint8 cmd[10] = {
	    FU_WEIDA_RAW_CMD8760_COMMAND9,
	    FU_WEIDA_RAW_CMD8760_SET_DEVICE_MODE,
	    mode,
	};
	return fu_weida_raw_w8760_command_write(self, cmd, 0, 10, error);
}

static gboolean
fu_weida_raw_w8760_command_read(FuWeidaRawDevice *self,
				guint8 *cmd,
				int cmd_size,
				guint8 *data,
				int start,
				int size,
				GError **error)
{
	int send_size;
	g_autofree guint8 *buf = NULL;

	if (size > 10)
		send_size = 64;
	else
		send_size = 10;

	if (!fu_weida_raw_device_set_feature(self, cmd, send_size, error))
		return FALSE;

	buf = g_malloc(send_size);
	if (size > 10)
		buf[0] = FU_WEIDA_RAW_CMD8760_COMMAND63;
	else
		buf[0] = FU_WEIDA_RAW_CMD8760_COMMAND9;

	if (!fu_weida_raw_device_get_feature(self, buf, send_size, error))
		return FALSE;

	memcpy(&data[start], &buf[1], send_size - 1);
	return TRUE;
}

static gboolean
fu_weida_raw_w8760_get_status(FuWeidaRawDevice *self,
			      guint8 *buf,
			      guint8 offset,
			      guint8 size,
			      GError **error)
{
	guint8 cmd[10] = {
	    FU_WEIDA_RAW_CMD8760_COMMAND9,
	    FU_WEIDA_RAW_CMD8760_GET_DEVICE_STATUS,
	    offset,
	    size,
	};
	return fu_weida_raw_w8760_command_read(self, cmd, sizeof(cmd), buf, 0, 10, error);
}

// XXX
static guint8
fu_weida_raw_w8760_get_device_mode(FuWeidaRawDevice *self, GError **error)
{
	guint8 status[10] = {0};

	if (fu_weida_raw_w8760_get_status(self, status, 4, 1, error)) {
		g_debug("status = %d", status[0]);
		return status[0];
	}

	return 0;
}

static gboolean
fu_weida_raw_w8760_protect_flash(FuWeidaRawDevice *self, guint16 protect_mask, GError **error)
{
	guint16 mask = protect_mask;
	guint8 cmd[10] = {
	    FU_WEIDA_RAW_CMD8760_COMMAND9,
	    FU_WEIDA_RAW_CMD8760_PROTECT_FLASH,
	};
	fu_memwrite_uint16(&cmd[2], mask, G_LITTLE_ENDIAN);
	fu_memwrite_uint16(&cmd[4], ~mask, G_LITTLE_ENDIAN);

	return fu_weida_raw_device_set_feature(self, cmd, sizeof(cmd), error);
}

typedef struct {
	guint8 cmd;
} FuWeidaRawDeviceReq;

static gboolean
fu_weida_w8760_set_n_check_device_mode_cb(FuDevice *device, gpointer user_data, GError **error)
{
	FuWeidaRawDevice *self = FU_WEIDA_RAW_DEVICE(device);
	FuWeidaRawDeviceReq *req = (FuWeidaRawDeviceReq *)user_data;

	if (!fu_weida_raw_w8760_set_device_mode(self, req->cmd, error))
		return FALSE;
	fu_device_sleep(device, 30);

	if (fu_weida_raw_w8760_get_device_mode(self, error) != req->cmd) {
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
	int status;
	guint8 status_buf[10] = {0};

	if (fu_weida_raw_w8760_get_status(self, &status_buf[0], 0, 1, error) <= 0) {
		g_prefix_error(error, "failed to wait-cmd-end: ");
		return FALSE;
	}

	status = status_buf[0];
	if ((status & 0x01) != 0) {
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
	guint8 cmd[10] = {
	    FU_WEIDA_RAW_CMD8760_COMMAND9,
	    FU_WEIDA_RAW_CMD8760_ERASE_FLASH,
	    (guint8)(address >> 12),
	    (guint8)(((address & 0x0FFF) + size + 4095) >> 12),
	};

	if (!fu_weida_raw_device_set_feature(self, cmd, sizeof(cmd), error))
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
	guint8 cmd[10] = {
	    FU_WEIDA_RAW_CMD8760_COMMAND9,
	    FU_WEIDA_RAW_CMD8760_SET_FLASH_ADDRESS,
	};
	fu_memwrite_uint32(&cmd[2], address, G_LITTLE_ENDIAN);
	return fu_weida_raw_w8760_command_write(self, cmd, 0, 10, error);
}

static gboolean
fu_weida_raw_w8760_batch_write_flash(FuWeidaRawDevice *self,
				     const guint8 *buf,
				     gsize start,
				     guint8 size,
				     GError **error)
{
	guint8 cmd[FU_WEIDA_RAW_USB_MAX_PAYLOAD_SIZE + 1] = {
	    FU_WEIDA_RAW_CMD8760_COMMAND63,
	    FU_WEIDA_RAW_CMD8760_WRITE_FLASH,
	    size,
	};

	/* sanity check */
	if (size > FU_WEIDA_RAW_USB_MAX_PAYLOAD_SIZE - 2) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INVALID_DATA,
			    "failed to write flash size 0x%x",
			    size);
		return FALSE;
	}
	memcpy(&cmd[3], &buf[start], size);

	if (!fu_weida_raw_w8760_command_write(self, cmd, 0, size + 3, error))
		return FALSE;
	if (!fu_device_retry_full(FU_DEVICE(self),
				  fu_weida_w8760_wait_cmd_end_cb,
				  200,
				  30,
				  NULL,
				  error))
		return FALSE;

	/* success */
	return TRUE;
}

static gboolean
fu_weida_raw_w8760_read_items(FuWeidaRawDevice *self,
			      guint8 cmd_id,
			      guint8 *buffer,
			      int start,
			      int item_size,
			      guint8 item_count,
			      GError **error)
{
	guint8 cmd[] = {FU_WEIDA_RAW_CMD8760_COMMAND9, cmd_id, item_count};
	return fu_weida_raw_w8760_command_read(self,
					       cmd,
					       2,
					       buffer,
					       start,
					       item_size * item_count,
					       error);
}

static gboolean
fu_weida_raw_w8760_read_buf_response(FuWeidaRawDevice *self, guint8 *data, int size, GError **error)
{
	return fu_weida_raw_w8760_read_items(self,
					     FU_WEIDA_RAW_CMD8760_READ_BUFFERED_RESPONSE,
					     data,
					     0,
					     sizeof(guint8),
					     size,
					     error);
}

static gboolean
fu_weida_raw_w8760_checksum_flash(FuWeidaRawDevice *self,
				  guint16 *pchksum,
				  guint32 flash_address,
				  guint32 size,
				  guint16 init_sum,
				  GError **error)
{
	guint8 cmd[10] = {
	    FU_WEIDA_RAW_CMD8760_COMMAND9,
	    FU_WEIDA_RAW_CMD8760_CALCULATE_FLASH_CHECKSUM,
	};
	guint8 buf[4] = {0};

	fu_memwrite_uint32(&cmd[2], flash_address, G_LITTLE_ENDIAN);
	fu_memwrite_uint32(&cmd[5], size, G_LITTLE_ENDIAN);
	fu_memwrite_uint16(&cmd[8], init_sum, G_LITTLE_ENDIAN);

	if (fu_weida_raw_w8760_command_write(self, cmd, 0, sizeof(cmd), error) <= 0)
		return FALSE;

	if (!fu_device_retry_full(FU_DEVICE(self),
				  fu_weida_w8760_wait_cmd_end_cb,
				  200,
				  30,
				  NULL,
				  error))
		return FALSE;

	if (fu_weida_raw_w8760_read_buf_response(self, buf, 2, error) <= 0)
		return FALSE;

	*pchksum = fu_memread_uint16(buf, G_LITTLE_ENDIAN);
	return TRUE;
}

static gboolean
fu_weida_raw_w8760_write_flash(FuWeidaRawDevice *self,
			       guint32 addr,
			       const guint8 *buf,
			       int start,
			       int size,
			       GError **error)
{
	gsize byte_count = size;
	gsize offset = start;
	gsize max_payload_size = FU_WEIDA_RAW_USB_MAX_PAYLOAD_SIZE - 2;
	guint32 cur_addr = 0xFFFFFFFF;

	// XXX
	while (byte_count >= max_payload_size) {
		if (!fu_weida_raw_block_is_empty(&buf[offset], max_payload_size)) {
			if (cur_addr != addr) {
				if (!fu_weida_raw_w8760_set_flash_address(self, addr, error))
					return FALSE;
			}
			if (fu_weida_raw_w8760_batch_write_flash(self,
								 buf,
								 offset,
								 max_payload_size,
								 error) <= 0)
				return FALSE;
			cur_addr = addr + max_payload_size;
		} else
			g_debug("Already ff, no need to set: 0x%x\n", addr);
		offset += max_payload_size;
		byte_count -= max_payload_size;
		addr += max_payload_size;
	}

	if (cur_addr != addr) {
		if (!fu_weida_raw_w8760_set_flash_address(self, addr, error))
			return FALSE;
	}

	if (byte_count > 0)
		return fu_weida_raw_w8760_batch_write_flash(self, buf, offset, byte_count, error);

	/* success */
	return TRUE;
}

static gboolean
fu_weida_raw_w8760_write_flash_page(FuWeidaRawDevice *self,
				    guint32 addr,
				    GBytes *blob,
				    GError **error)
{
	// FIXME: this needs to be ported to fu_chunk_array_new_from_bytes()
	unsigned int write_base;
	unsigned int write_size;
	gsize bufsz = 0;
	const guint8 *buf = g_bytes_get_data(blob, &bufsz);

	write_base = FU_WEIDA_RAW_FLASH_PAGE_SIZE;

	if (addr & 0xFF) {
		unsigned int size_partial;
		size_partial = write_base - (addr & 0xFF);

		if (bufsz > (gsize)size_partial) {
			write_size = size_partial;
			bufsz = bufsz - size_partial;
		} else {
			write_size = bufsz;
			bufsz = 0;
		}
		if (!fu_weida_raw_w8760_write_flash(self, addr, buf, 0, write_size, error))
			return FALSE;
		buf = buf + size_partial;
		addr = addr + size_partial;
	}

	while (bufsz) {
		if ((addr & 0xfff) == 0)
			g_message("base addr: 0x%x\n", addr);
		if (bufsz > (gsize)write_base) {
			write_size = write_base;
			bufsz = bufsz - write_base;
		} else {
			write_size = bufsz;
			bufsz = 0;
		}
		if (!fu_weida_raw_w8760_write_flash(self, addr, buf, 0, write_size, error))
			return FALSE;
		buf = buf + write_base;
		addr = addr + write_base;
	}

	/* success */
	return TRUE;
}

static gboolean
fu_weida_raw_w8760_flash_write_data(FuWeidaRawDevice *self,
				    guint32 address,
				    GBytes *blob,
				    GError **error)
{
	if ((address & 0x3) != 0) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_NOT_SUPPORTED,
				    "address alignment bad");
		return FALSE;
	}
	if (!fu_weida_raw_w8760_set_flash_address(self, address, error))
		return FALSE;
	return fu_weida_raw_w8760_write_flash_page(self, address, blob, error);
}

static gboolean
fu_weida_raw_w8760_dev_reboot(FuWeidaRawDevice *self, GError **error)
{
	guint8 cmd[] = {
	    FU_WEIDA_RAW_CMD8760_COMMAND9,
	    FU_WEIDA_RAW_CMD8760_REBOOT,
	    0xB9,
	    0x0C,
	    0x8A,
	    0x24,
	};
	return fu_weida_raw_w8760_command_write(self, cmd, 0, sizeof(cmd), error);
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
fu_weida_raw_w8760_write_chunk_cb(FuDevice *device, gpointer user_data, GError **error)
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
					       0,
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
fu_weida_raw_w8760_write_chunk(FuWeidaRawDevice *self,
			       gsize address,
			       GInputStream *stream,
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
	for (guint i = 0; i < fu_chunk_array_length(chunks); i++) {
		g_autoptr(FuChunk) chk = NULL;
		chk = fu_chunk_array_index(chunks, i, error);
		if (chk == NULL)
			return FALSE;
		if (!fu_device_retry(FU_DEVICE(self),
				     fu_weida_raw_w8760_write_chunk_cb,
				     5,
				     chk,
				     error))
			return FALSE;
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
	g_autoptr(GPtrArray) imgs = fu_firmware_get_images(firmware);

	/* progress */
	fu_progress_set_id(progress, G_STRLOC);
	fu_progress_add_flag(progress, FU_PROGRESS_FLAG_GUESSED);
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_WRITE, 33, NULL);
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_WRITE, 35, NULL);
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_WRITE, 35, NULL);

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

	for (guint i = 0; i < imgs->len; i++) {
		FuFirmware *img = g_ptr_array_index(imgs, i);
		g_autoptr(GInputStream) stream = NULL;

		stream = fu_firmware_get_stream(img, error);
		if (stream == NULL)
			return FALSE;
		if (!fu_weida_raw_w8760_write_chunk(self, fu_firmware_get_addr(img), stream, error))
			return FALSE;
	}
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

	/* sanity check */
	if (self->dev_type != FU_WEIDA_RAW_FW8760) {
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
	fu_udev_device_add_flag(FU_UDEV_DEVICE(self), FU_UDEV_DEVICE_FLAG_OPEN_READ);
	fu_udev_device_add_flag(FU_UDEV_DEVICE(self), FU_UDEV_DEVICE_FLAG_OPEN_WRITE);
	fu_udev_device_add_flag(FU_UDEV_DEVICE(self), FU_UDEV_DEVICE_FLAG_OPEN_NONBLOCK);
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
}
