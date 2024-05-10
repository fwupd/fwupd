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
#include "fu-weida-raw-struct.h"

/* this can be set using Flags=example in the quirk file  */
#define FU_WEIDA_RAW_DEVICE_FLAG_EXAMPLE (1 << 0)

#define FU_WEIDA_RAW_DEVICE_IOCTL_TIMEOUT 5000 /* ms */
#define FU_WEIDA_RAW_REQ_DEV_INFO	  0xF2
#define FU_WEIDA_RAW_RETRY_COUNT	  5
#define FU_WEIDA_RAW_PAGE_SIZE		  0x1000
#define FU_WEIDA_RAW_FLASH_PAGE_SIZE	  256
#define FU_WEIDA_RAW_USB_MAX_PAYLOAD_SIZE 63

#define FU_WEIDA_RAW_FOURCC_ID_RIFF 0x46464952
#define FU_WEIDA_RAW_FOURCC_ID_WIF2 0x32464957
#define FU_WEIDA_RAW_FOURCC_ID_WHIF 0x46494857
#define FU_WEIDA_RAW_FOURCC_ID_INFO 0x4f464e49
#define FU_WEIDA_RAW_FOURCC_ID_FSUM 0x4d555346
#define FU_WEIDA_RAW_FOURCC_ID_FERA 0x41524546
#define FU_WEIDA_RAW_FOURCC_ID_FBIN 0x4e494246
#define FU_WEIDA_RAW_FOURCC_ID_FRMT 0x544D5246
#define FU_WEIDA_RAW_FOURCC_ID_FRWR 0x52575246
#define FU_WEIDA_RAW_FOURCC_ID_CNFG 0x47464E43

typedef struct {
	guint32 FourCC;
	guint32 FileSize;
	guint32 DataType;
} FuWeidaRiffHeader;

typedef struct {
	guint32 FourCC;
	guint32 Size; // payload size
} FuWeidaChunkHeader;

typedef struct {
	guint32 Address;
	guint32 Size;
} FuWeidaFlashSpace;

typedef struct {
	guint32 Address;
	guint32 Size;
	guint8 *bin;
} FuWeidaSpiBinary;

typedef enum {
	FU_WDT8760_COMMAND9 = 0x06,
	FU_WDT8760_COMMAND63 = 0x07,
	FU_WDT8760_MODE_FLASH_PROGRAM = 0x96,
	FU_WDT8760_READ_BUFFERED_RESPONSE = 0xC7,
	FU_WDT8760_GET_DEVICE_STATUS = 0xC9,
	FU_WDT8760_SET_DEVICE_MODE = 0xCA,
	FU_WDT8760_REBOOT = 0xCE,
	FU_WDT8760_SET_FLASH_ADDRESS = 0xD0,
	FU_WDT8760_READ_FLASH = 0xD1,
	FU_WDT8760_ERASE_FLASH = 0xD2,
	FU_WDT8760_WRITE_FLASH = 0xD3,
	FU_WDT8760_PROTECT_FLASH = 0xD4,
	FU_WDT8760_CALCULATE_FLASH_CHECKSUM = 0xD5,

} FU_WDT8760Command;

typedef enum {
	FU_WDT8760_UnprotectLower508k = 0x0044,
	FU_WDT8760_ProtectAll512k = 0x007C,
} FU_WDT8760_FlashProtect;

#define FU_WEIDA_RAW_FW_NOT_SUPPORT 0x00
#define FU_WEIDA_RAW_FW_MAYBE_ISP   0x01
#define FU_WEIDA_RAW_FW8755	    0x02
#define FU_WEIDA_RAW_FW8760	    0x4
#define FU_WEIDA_RAW_FW8762	    0x08
#define FU_WEIDA_RAW_FW8790	    0X10

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

static guint32
fu_weida_raw_check_firmware_id(FuWeidaRawDevice *self, guint32 fwid)
{
	if ((fwid & 0xF0000000) == 0x30000000) {
		g_message("It is WDT8752 fw !\n");
		fu_device_set_summary(FU_DEVICE(self), "CI3.0_SR2.0");

		return FU_WEIDA_RAW_FW8755;
	}

	if ((fwid & 0xFFFF0000) == 0xFFFF0000) {
		fu_device_set_summary(FU_DEVICE(self), "CI3.0_SR2.0");
		return FU_WEIDA_RAW_FW8755;
	}

	if ((fwid & 0xF0000000) == 0x40000000) {
		fu_device_set_summary(FU_DEVICE(self), "CI4.0_TM4.0");
		return FU_WEIDA_RAW_FW8760;
	}
	if ((fwid & 0xF0000000) == 0x50000000) {
		fu_device_set_summary(FU_DEVICE(self), "CI5.0");
		return FU_WEIDA_RAW_FW8790;
	}

	return FU_WEIDA_RAW_FW_NOT_SUPPORT;
}

static gboolean
fu_weida_raw_device_ensure_status(FuWeidaRawDevice *self, GError **error)
{
	guint8 buf[] = {[0] = FU_WEIDA_RAW_REQ_DEV_INFO, [1 ... 63] = 0xff};

	if (!fu_weida_raw_device_get_feature(self, buf, sizeof(buf), error))
		return FALSE;
	self->firmware_id = fu_memread_uint32(&buf[1], G_LITTLE_ENDIAN);
	self->hardware_id = fu_memread_uint32(&buf[5], G_LITTLE_ENDIAN);
	self->serial_number = fu_memread_uint32(&buf[9], G_LITTLE_ENDIAN);
	self->dev_type = fu_weida_raw_check_firmware_id(self, self->firmware_id);
	if (self->firmware_id == 0) {
		self->dev_type = self->dev_type | FU_WEIDA_RAW_FW_MAYBE_ISP;
	}
	self->firmware_rev_ext = 0;
	if (self->dev_type == FU_WEIDA_RAW_FW8760)
		self->firmware_rev_ext = buf[33];
	else if (self->dev_type == FU_WEIDA_RAW_FW8790)
		self->firmware_rev_ext = buf[14];

	if (self->dev_type == FU_WEIDA_RAW_FW8755) {
		fu_device_set_version(
		    FU_DEVICE(self),
		    fu_version_from_uint16(self->firmware_id, FWUPD_VERSION_FORMAT_HEX));
	} else {
		fu_device_set_version(FU_DEVICE(self),
				      fu_version_from_uint16(((self->firmware_id & 0x0FFF) << 4) |
								 (self->firmware_rev_ext & 0x000F),
							     FWUPD_VERSION_FORMAT_HEX));
	}
	fu_device_set_serial(FU_DEVICE(self),
			     fu_version_from_uint32(self->serial_number, FWUPD_VERSION_FORMAT_HEX));

	return TRUE;
}

static gboolean
fu_weida_raw_device_setup(FuDevice *device, GError **error)
{
	FuWeidaRawDevice *self = FU_WEIDA_RAW_DEVICE(device);
	g_debug("weida_raw_setup");

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
	guint8 cmd[10] = {0};

	cmd[0] = (guint8)FU_WDT8760_COMMAND9;
	cmd[1] = (guint8)FU_WDT8760_SET_DEVICE_MODE;
	cmd[2] = mode;
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
	if (size > 10)
		send_size = 64;
	else
		send_size = 10;
	if (fu_weida_raw_device_set_feature(self, cmd, send_size, error) == TRUE) {
		g_autofree guint8 *buf = g_malloc(send_size);
		if (size > 10)
			buf[0] = FU_WDT8760_COMMAND63;
		else
			buf[0] = FU_WDT8760_COMMAND9;

		if (fu_weida_raw_device_get_feature(self, buf, send_size, error) == TRUE) {
			memcpy(&data[start], &buf[1], send_size - 1);
			return TRUE;
		} else {
			return FALSE;
		}
	} else {
		return FALSE;
	}
}

static gboolean
fu_weida_raw_w8760_get_status(FuWeidaRawDevice *self,
			      guint8 *buf,
			      guint8 offset,
			      guint8 size,
			      GError **error)
{
	guint8 cmd[10] = {0};
	cmd[0] = (gint8)FU_WDT8760_COMMAND9;
	cmd[1] = (gint8)FU_WDT8760_GET_DEVICE_STATUS;
	cmd[2] = offset;
	cmd[3] = size;

	return fu_weida_raw_w8760_command_read(self, cmd, sizeof(cmd), buf, 0, 10, error);
}

static guint8
fu_weida_raw_w8760_get_device_mode(FuWeidaRawDevice *self, GError **error)
{
	guint8 status[10] = {0};
	if (!self)
		return 0;

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

	guint8 cmd[10] = {FU_WDT8760_COMMAND9, FU_WDT8760_PROTECT_FLASH, 0, 0, 0, 0, 0};
	fu_memwrite_uint16(&cmd[2], mask, G_LITTLE_ENDIAN);
	fu_memwrite_uint16(&cmd[4], ~mask, G_LITTLE_ENDIAN);

	if (!fu_weida_raw_device_set_feature(self, cmd, sizeof(cmd), error))
		return FALSE;
	else
		return TRUE;
}

static gboolean
fu_weida_w8760_set_n_check_device_mode(FuWeidaRawDevice *pdev,
				       guint8 mode,
				       int timeout_ms,
				       int intv_ms,
				       GError **error)
{
	int polling_timeout_ms = 300 * 1000;
	int polling_intv_ms = 30;

	if (timeout_ms)
		polling_timeout_ms = timeout_ms;
	if (intv_ms)
		polling_intv_ms = intv_ms;

	do {
		if (fu_weida_raw_w8760_set_device_mode(pdev, mode, error)) {
			if (fu_weida_raw_w8760_get_device_mode(pdev, error) == mode)
				return TRUE;
		}

		g_usleep(polling_intv_ms * 1000);
		polling_timeout_ms -= polling_intv_ms;
	} while (polling_timeout_ms > 0);

	return FALSE;
}

static gboolean
fu_weida_raw_w8760_wait_cmd_end(FuWeidaRawDevice *self, int timeout_ms, int invt_ms, GError **error)
{
	int polling_timeout_ms = 5000;
	int polling_intv_ms = 30;
	int status;
	guint8 status_buf[10] = {0};

	if (timeout_ms)
		polling_timeout_ms = timeout_ms;
	if (invt_ms)
		polling_intv_ms = invt_ms;

	do {
		if (fu_weida_raw_w8760_get_status(self, &status_buf[0], 0, 1, error) <= 0) {
			g_debug("fail %s \n", __func__);
			return FALSE;
		}

		status = status_buf[0];
		if ((status & 0x01) == 0) {
			return TRUE;
		}

		fu_device_sleep(FU_DEVICE(self), polling_intv_ms);
		polling_timeout_ms -= polling_intv_ms;
	} while (polling_timeout_ms > 0);
	g_debug("wait_cmd_end timeout fail");

	return FALSE;
}

static gboolean
fu_weida_raw_w8760_erase_flash(FuWeidaRawDevice *self,
			       guint32 address,
			       guint32 size,
			       GError **error)
{
	guint8 cmd[10] = {0};

	cmd[0] = FU_WDT8760_COMMAND9;
	cmd[1] = FU_WDT8760_ERASE_FLASH;
	cmd[2] = (guint8)(address >> 12);
	cmd[3] = (guint8)(((address & 0x0FFF) + size + 4095) >> 12);

	if (!fu_weida_raw_device_set_feature(self, cmd, sizeof(cmd), error)) {
		return FALSE;
	} else {
		if (!fu_weida_raw_w8760_wait_cmd_end(self, 0, 0, error)) {
			return FALSE;
		}
	}
	return TRUE;
}

static gboolean
fu_weida_raw_w8760_set_flash_address(FuWeidaRawDevice *self, guint32 address, GError **error)
{
	guint8 cmd[10] = {0};
	memset(&cmd, 0, sizeof(cmd));
	cmd[0] = FU_WDT8760_COMMAND9;
	cmd[1] = FU_WDT8760_SET_FLASH_ADDRESS;
	fu_memwrite_uint32(&cmd[2], address, G_LITTLE_ENDIAN);

	return fu_weida_raw_w8760_command_write(self, cmd, 0, 10, error);
}

static gboolean
fu_weida_raw_w8760_batch_write_flash(FuWeidaRawDevice *self,
				     const guint8 *buf,
				     int start,
				     guint8 size,
				     GError **error)
{
	guint8 cmd[FU_WEIDA_RAW_USB_MAX_PAYLOAD_SIZE + 1] = {0};

	if (size > FU_WEIDA_RAW_USB_MAX_PAYLOAD_SIZE - 2) {
		g_debug("%s: payload data overrun\n", __func__);
		return FALSE;
	}

	cmd[0] = FU_WDT8760_COMMAND63;
	cmd[1] = (guint8)FU_WDT8760_WRITE_FLASH;
	cmd[2] = (guint8)size;
	memcpy(&cmd[3], &buf[start], size);

	if (fu_weida_raw_w8760_command_write(self, cmd, 0, size + 3, error) > 0)
		return fu_weida_raw_w8760_wait_cmd_end(self, 0, 0, error);

	return FALSE;
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
	int size = item_size * item_count;
	guint8 cmd[] = {FU_WDT8760_COMMAND9, cmd_id, item_count};

	return fu_weida_raw_w8760_command_read(self, cmd, 2, buffer, start, size, error);
}

static gboolean
fu_weida_raw_w8760_read_buf_response(FuWeidaRawDevice *self, guint8 *data, int size, GError **error)
{
	return fu_weida_raw_w8760_read_items(self,
					     FU_WDT8760_READ_BUFFERED_RESPONSE,
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
	guint8 cmd[10];
	guint8 buf[4];

	cmd[0] = FU_WDT8760_COMMAND9;
	cmd[1] = (guint8)FU_WDT8760_CALCULATE_FLASH_CHECKSUM;
	fu_memwrite_uint32(&cmd[2], flash_address, G_LITTLE_ENDIAN);
	fu_memwrite_uint32(&cmd[5], size, G_LITTLE_ENDIAN);
	fu_memwrite_uint16(&cmd[8], init_sum, G_LITTLE_ENDIAN);

	if (fu_weida_raw_w8760_command_write(self, cmd, 0, sizeof(cmd), error) <= 0)
		return FALSE;

	if (fu_weida_raw_w8760_wait_cmd_end(self, 0, 0, error) <= 0)
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
	int offset = start;
	gsize max_payload_size = FU_WEIDA_RAW_USB_MAX_PAYLOAD_SIZE - 2;
	guint32 cur_addr = 0xFFFFFFFF;

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

	return TRUE;
}

static gboolean
fu_weida_raw_w8760_write_flash_page(FuWeidaRawDevice *pdev,
				    guint32 addr,
				    const guint8 *buf,
				    int size,
				    GError **error)
{
	int retval = 0;
	unsigned int write_base;
	unsigned int write_size;

	write_base = FU_WEIDA_RAW_FLASH_PAGE_SIZE;

	if (addr & 0xFF) {
		unsigned int size_partial;
		size_partial = write_base - (addr & 0xFF);

		if (size > (int)size_partial) {
			write_size = size_partial;
			size = size - size_partial;
		} else {
			write_size = size;
			size = 0;
		}
		retval = fu_weida_raw_w8760_write_flash(pdev, addr, buf, 0, write_size, error);
		if (!retval)
			return retval;
		buf = buf + size_partial;
		addr = addr + size_partial;
	}

	while (size) {
		if ((addr & 0xfff) == 0)
			g_debug("base addr: 0x%x\n", addr);
		if (size > (int)write_base) {
			write_size = write_base;
			size = size - write_base;
		} else {
			write_size = size;
			size = 0;
		}
		retval = fu_weida_raw_w8760_write_flash(pdev, addr, buf, 0, write_size, error);
		if (!retval)
			return retval;
		buf = buf + write_base;
		addr = addr + write_base;
	}

	return 1;
}

static gboolean
fu_weida_raw_w8760_flash_write_data(FuWeidaRawDevice *pdev,
				    const guint8 *data,
				    guint32 address,
				    guint32 length,
				    GError **error)
{
	if (!pdev || !data)
		return FALSE;

	if ((address & 0x3) != 0)
		return FALSE;

	if (!fu_weida_raw_w8760_set_flash_address(pdev, address, error))
		return FALSE;

	return fu_weida_raw_w8760_write_flash_page(pdev, address, data, length, error);
}

static gboolean
fu_weida_raw_w8760_dev_reboot(FuWeidaRawDevice *pdev, GError **error)
{
	guint8 cmd[] = {FU_WDT8760_COMMAND9, FU_WDT8760_REBOOT, 0xB9, 0x0C, 0x8A, 0x24};

	return fu_weida_raw_w8760_command_write(pdev, cmd, 0, sizeof(cmd), error);
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

	g_debug("HID_PHYS: %s", hid_id);

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
fu_weida_raw_w8760_write_spi_bin(FuWeidaRawDevice *pdev, FuWeidaSpiBinary spi_bin, GError **error)
{
	guint w_size;
	guint8 *pdata;
	int start_addr;
	int page_size;
	int retry_count = 0;
	guint16 calc_checksum;
	guint16 read_checksum;

	if (!fu_weida_raw_w8760_erase_flash(pdev, spi_bin.Address, spi_bin.Size, error)) {
		g_debug("ERASE FLASH FAIL\n");
		return FALSE;
	}

	w_size = spi_bin.Size;
	pdata = spi_bin.bin;
	start_addr = spi_bin.Address;
	while (w_size) {
		if (w_size > FU_WEIDA_RAW_PAGE_SIZE) {
			page_size = FU_WEIDA_RAW_PAGE_SIZE;
			w_size = w_size - FU_WEIDA_RAW_PAGE_SIZE;
		} else {
			page_size = w_size;
			w_size = 0;
		}
		for (retry_count = 0; retry_count < FU_WEIDA_RAW_RETRY_COUNT; retry_count++) {
			fu_weida_raw_w8760_flash_write_data(pdev,
							    pdata,
							    start_addr,
							    page_size,
							    error);

			calc_checksum = fu_weida_raw_misr_for_bytes(0, pdata, 0, page_size);
			fu_weida_raw_w8760_checksum_flash(pdev,
							  &read_checksum,
							  start_addr,
							  page_size,
							  0,
							  error);
			if (read_checksum == calc_checksum)
				break;
			else
				g_debug("checksum failed (%d): %d <> %d\n",
					retry_count,
					read_checksum,
					calc_checksum);
		}
		if (retry_count == FU_WEIDA_RAW_RETRY_COUNT) {
			g_debug("retry fail %s", __func__);
			return FALSE;
		}

		start_addr = start_addr + page_size;
		pdata = pdata + page_size;
	}
	return TRUE;
}

static gboolean
fu_weida_raw_w8760_wif_chunk_write(FuWeidaRawDevice *self,
				   const guint8 *data,
				   gsize bufsz,
				   gsize start,
				   GError **error)
{
	FuWeidaChunkHeader hed1;
	FuWeidaSpiBinary spi_bin;
	gboolean ret = TRUE;
	gsize offset = start;
	g_debug("%s\n", __func__);

	if (!fu_weida_w8760_set_n_check_device_mode(self,
						    FU_WDT8760_MODE_FLASH_PROGRAM,
						    0,
						    0,
						    error)) {
		g_debug("SET DEVICE TO FLASH PROGRAM FAIL\n");
		return FALSE;
	}
	if (!fu_weida_raw_w8760_protect_flash(self, FU_WDT8760_UnprotectLower508k, error)) {
		g_debug("W8760_UnprotectLower508k fail\n");
		return FALSE;
	}

	while (offset < bufsz) {
		if (!fu_memread_uint32_safe(data,
					    bufsz,
					    offset,
					    &hed1.FourCC,
					    G_LITTLE_ENDIAN,
					    error)) {
			ret = FALSE;
			break;
		}
		offset += sizeof(guint32);

		if (!(hed1.FourCC == FU_WEIDA_RAW_FOURCC_ID_FRWR) &&
		    !(hed1.FourCC == FU_WEIDA_RAW_FOURCC_ID_CNFG)) {
			ret = FALSE;
			break;
		}

		if (!fu_memread_uint32_safe(data,
					    bufsz,
					    offset,
					    &hed1.Size,
					    G_LITTLE_ENDIAN,
					    error)) {
			ret = FALSE;
			break;
		}
		offset += sizeof(guint32);
		if (!fu_memread_uint32_safe(data,
					    bufsz,
					    offset,
					    &spi_bin.Address,
					    G_LITTLE_ENDIAN,
					    error)) {
			ret = FALSE;
			break;
		}
		offset += sizeof(guint32);

		if (!fu_memread_uint32_safe(data,
					    bufsz,
					    offset,
					    &spi_bin.Size,
					    G_LITTLE_ENDIAN,
					    error)) {
			ret = FALSE;
			break;
		}
		offset += sizeof(guint32);
		offset += 4 * sizeof(guint32);
		spi_bin.bin = g_malloc0(spi_bin.Size);
		memcpy(spi_bin.bin, &data[offset], spi_bin.Size);
		offset += spi_bin.Size;
		if (fu_weida_raw_w8760_write_spi_bin(self, spi_bin, error) == FALSE) {
			ret = FALSE;
			break;
		}
	}
	if (!fu_weida_raw_w8760_protect_flash(self, FU_WDT8760_ProtectAll512k, error)) {
		g_debug("W8760_UnprotectLower508k fail\n");
	}
	return ret;
}

static gboolean
fu_weida_raw_w8760_write_wif1(FuWeidaRawDevice *self,
			      const guint8 *data,
			      gsize bufsz,
			      GError **error)
{
	FuWeidaRiffHeader he;
	FuWeidaChunkHeader hed1;

	gsize offset = 0;
	g_message("%s\n", __func__);

	if (!fu_memread_uint32_safe(data, bufsz, offset, &he.FourCC, G_LITTLE_ENDIAN, error))
		return FALSE;
	offset += sizeof(guint32);
	if (he.FourCC != FU_WEIDA_RAW_FOURCC_ID_RIFF)
		return FALSE;
	if (!fu_memread_uint32_safe(data, bufsz, offset, &he.FileSize, G_LITTLE_ENDIAN, error))
		return FALSE;
	offset += sizeof(guint32);

	if (!fu_memread_uint32_safe(data, bufsz, offset, &he.DataType, G_LITTLE_ENDIAN, error))
		return FALSE;
	offset += sizeof(guint32);

	if (he.DataType == FU_WEIDA_RAW_FOURCC_ID_WIF2) {
		return FALSE;
	} else if (he.DataType == FU_WEIDA_RAW_FOURCC_ID_WHIF) {
		if (!fu_memread_uint32_safe(data,
					    bufsz,
					    offset,
					    &hed1.FourCC,
					    G_LITTLE_ENDIAN,
					    error))
			return FALSE;
		offset += sizeof(guint32);
		if (hed1.FourCC != FU_WEIDA_RAW_FOURCC_ID_FRMT) {
			return FALSE;
		}
		if (!fu_memread_uint32_safe(data,
					    bufsz,
					    offset,
					    &hed1.Size,
					    G_LITTLE_ENDIAN,
					    error))
			return FALSE;
		offset += hed1.Size - sizeof(guint32);
		fu_weida_raw_w8760_wif_chunk_write(self, data, bufsz, offset, error);
		return FALSE;
	}
	return FALSE;
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
	/* TODO: anything the device has to do when the update has completed */
	g_assert(self != NULL);
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
	g_autoptr(GInputStream) stream = NULL;
	g_autoptr(FuChunkArray) chunks = NULL;
	g_autoptr(GBytes) blob = NULL;
	gsize test_size;
	const guint8 *buf;

	/* progress */
	fu_progress_set_id(progress, G_STRLOC);
	fu_progress_add_flag(progress, FU_PROGRESS_FLAG_GUESSED);
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_WRITE, 44, NULL);
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_VERIFY, 35, NULL);

	/* get default image */
	// stream = fu_firmware_get_stream(firmware, error);
	// if (stream == NULL)
	//	return FALSE;

	/* write each block */

	blob = fu_firmware_get_bytes(firmware, error);
	if (blob == NULL)
		return FALSE;

	/* get default image */
	g_return_val_if_fail(blob != NULL, G_MAXUINT8);
	test_size = g_bytes_get_size(blob);
	buf = g_bytes_get_data(blob, &test_size);

	if (self->dev_type == FU_WEIDA_RAW_FW8760) {
		g_debug("It is w8760 device");
		fu_weida_raw_w8760_write_wif1(self, buf, test_size, error);
		fu_weida_raw_w8760_dev_reboot(self, error);
	} else {
		return FALSE;
	}

	g_usleep(2 * 1000 * 1000);

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
	FuWeidaRawDevice *self = FU_WEIDA_RAW_DEVICE(device);

	/* TODO: parse value from quirk file */

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
	const gchar *i2c_id;
	const gchar *back_id;
	fu_device_set_version_format(FU_DEVICE(self), FWUPD_VERSION_FORMAT_HEX);
	fu_device_set_remove_delay(FU_DEVICE(self), FU_DEVICE_REMOVE_DELAY_RE_ENUMERATE);
	fu_device_add_protocol(FU_DEVICE(self), "com.weida.raw");
	fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_UPDATABLE);
	fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_UNSIGNED_PAYLOAD);
	fu_device_add_internal_flag(FU_DEVICE(self), FU_DEVICE_INTERNAL_FLAG_ONLY_WAIT_FOR_REPLUG);
	fu_device_add_icon(FU_DEVICE(self), "input-tablet");
	fu_device_set_name(FU_DEVICE(self), "CoolTouch Device");
	fu_device_set_vendor(FU_DEVICE(self), "WEIDA");

	fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_REQUIRE_AC);
	if (FU_DEVICE(self) != NULL) {
		back_id = fu_device_get_backend_id(FU_DEVICE(self));
		if (back_id != NULL) {
			i2c_id = g_strrstr(fu_device_get_backend_id(FU_DEVICE(self)), "i2c");
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
