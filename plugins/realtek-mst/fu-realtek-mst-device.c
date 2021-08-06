/*
 * Copyright (C) 2021 Peter Marheine <pmarheine@chromium.org>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#ifdef HAVE_ERRNO_H
#include <errno.h>
#endif
#include <fcntl.h>
#include <glib/gstdio.h>
#include <linux/i2c-dev.h>

#include "fu-realtek-mst-device.h"

/* firmware debug address */
#define I2C_ADDR_DEBUG			0x35
/* programming address */
#define I2C_ADDR_ISP			0x4a

/* some kind of operation attribute bits */
#define REG_CMD_ATTR			0x60
/* write set to begin executing, cleared when done */
#define CMD_ERASE_BUSY			0x01

/* 24-bit address for commands */
#define REG_CMD_ADDR_HI			0x64
#define REG_CMD_ADDR_MID		0x65
#define REG_CMD_ADDR_LO			0x66

/* register for erase commands */
#define REG_ERASE_OPCODE		0x61
#define CMD_OPCODE_ERASE_SECTOR		0x20
#define CMD_OPCODE_ERASE_BLOCK		0xD8

/* register for read commands */
#define REG_READ_OPCODE			0x6A
#define CMD_OPCODE_READ			0x03

/* register for write commands */
#define REG_WRITE_OPCODE		0x6D
#define CMD_OPCODE_WRITE		0x02

/* mode register address */
#define REG_MCU_MODE			0x6F
/* when bit is set in mode register, ISP mode is active */
#define MCU_MODE_ISP			(1 << 7)
/* write set to begin write, reset by device when complete */
#define MCU_MODE_WRITE_BUSY		(1 << 5)
/* when bit is clear, write buffer contains data */
#define MCU_MODE_WRITE_BUF		(1 << 4)

/* write data into write buffer */
#define REG_WRITE_FIFO			0x70
/* number of bytes to write minus 1 (0xff means 256 bytes) */
#define REG_WRITE_LEN			0x71

/* Indirect registers allow access to registers with 16-bit addresses. Write
 * 0x9F to the LO register, then the top byte of the address to HI, the
 * bottom byte of the address to LO, then read or write HI to read or write
 * the value of the target register. */
#define REG_INDIRECT_LO			0xF4
#define REG_INDIRECT_HI			0xF5

/* GPIO configuration/access registers */
#define REG_GPIO88_CONFIG		0x104F
#define REG_GPIO88_VALUE		0xFE3F

/* flash chip properties */
#define FLASH_SIZE			0x100000
#define FLASH_SECTOR_SIZE		4096
#define FLASH_BLOCK_SIZE		65536

/* MST flash layout */
#define FLASH_USER1_ADDR		0x10000
#define FLASH_FLAG1_ADDR		0xfe304
#define FLASH_USER2_ADDR		0x80000
#define FLASH_FLAG2_ADDR		0xff304
#define FLASH_USER_SIZE			0x70000

enum dual_bank_mode {
	DUAL_BANK_USER_ONLY		= 0,
	DUAL_BANK_DIFF			= 1,
	DUAL_BANK_COPY			= 2,
	DUAL_BANK_USER_ONLY_FLAG	= 3,
	DUAL_BANK_MAX_VALUE		= 3,
};

enum flash_bank {
	FLASH_BANK_BOOT			= 0,
	FLASH_BANK_USER1		= 1,
	FLASH_BANK_USER2		= 2,
	FLASH_BANK_MAX_VALUE		= 2,
	FLASH_BANK_INVALID		= 255,
};

struct dual_bank_info {
	gboolean		 is_enabled;
	enum dual_bank_mode	 mode;
	enum flash_bank		 active_bank;
	guint8			 user1_version[2];
	guint8			 user2_version[2];
};

struct _FuRealtekMstDevice {
	FuI2cDevice		 parent_instance;
	gchar			*dp_aux_dev_name;
	gchar *dp_card_kernel_name;
	enum flash_bank		 active_bank;
};

G_DEFINE_TYPE (FuRealtekMstDevice, fu_realtek_mst_device, FU_TYPE_I2C_DEVICE)

static gboolean
fu_realtek_mst_device_set_quirk_kv (FuDevice *device,
				    const gchar *key,
				    const gchar *value,
				    GError **error)
{
	FuRealtekMstDevice *self = FU_REALTEK_MST_DEVICE (device);

	if (g_strcmp0 (key, "RealtekMstDpAuxName") == 0) {
		self->dp_aux_dev_name = g_strdup (value);
	} else if (g_strcmp0(key, "RealtekMstDrmCardKernelName") == 0) {
		self->dp_card_kernel_name = g_strdup(value);
	} else {
		g_set_error (error, FWUPD_ERROR, FWUPD_ERROR_NOT_SUPPORTED,
			     "unsupported quirk key: %s", key);
		return FALSE;
	}
	return TRUE;
}

static FuUdevDevice *
locate_i2c_bus(const GPtrArray *i2c_devices)
{
	for (guint i = 0; i < i2c_devices->len; i++) {
		FuUdevDevice *i2c_device = g_ptr_array_index(i2c_devices, i);
		FuUdevDevice *bus_device;
		g_autoptr(GPtrArray) i2c_buses =
		    fu_udev_device_get_children_with_subsystem(i2c_device, "i2c-dev");

		if (i2c_buses->len == 0) {
			g_debug("no i2c-dev found under %s",
				fu_udev_device_get_sysfs_path(i2c_device));
			continue;
		}
		if (i2c_buses->len > 1) {
			g_debug("ignoring %u additional i2c-dev under %s",
				i2c_buses->len - 1,
				fu_udev_device_get_sysfs_path(i2c_device));
		}

		bus_device = g_object_ref(g_ptr_array_index(i2c_buses, 0));
		g_debug("Found I2C bus at %s, using this device",
			fu_udev_device_get_sysfs_path(bus_device));
		return bus_device;
	}
	return NULL;
}

static gboolean
fu_realtek_mst_device_use_aux_dev(FuRealtekMstDevice *self, GError **error)
{
	g_autoptr(GUdevClient) udev_client = g_udev_client_new (NULL);
	g_autoptr(GUdevEnumerator) udev_enumerator = g_udev_enumerator_new (udev_client);
	g_autoptr(GList) matches = NULL;
	FuUdevDevice *bus_device = NULL;

	g_udev_enumerator_add_match_subsystem (udev_enumerator, "drm_dp_aux_dev");
	g_udev_enumerator_add_match_sysfs_attr (udev_enumerator, "name",
						self->dp_aux_dev_name);
	matches = g_udev_enumerator_execute (udev_enumerator);

	/* from a drm_dp_aux_dev with the given name, locate its sibling i2c
	 * device and in turn the i2c-dev under that representing the actual
	 * I2C bus that runs over DPDDC on the port represented by the
	 * drm_dp_aux_dev */
	for (GList *element = matches; element != NULL; element = element->next) {
		g_autoptr(FuUdevDevice) device = fu_udev_device_new (element->data);
		g_autoptr(GPtrArray) i2c_devices = NULL;

		if (bus_device != NULL) {
			g_debug ("Ignoring additional aux device %s",
				 fu_udev_device_get_sysfs_path (device));
			continue;
		}

		i2c_devices = fu_udev_device_get_siblings_with_subsystem (device, "i2c");
		bus_device = locate_i2c_bus(i2c_devices);
	}

	if (bus_device == NULL) {
		g_set_error (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_NOT_SUPPORTED,
			     "did not find an i2c-dev associated with DP aux \"%s\"",
			     self->dp_aux_dev_name);
		return FALSE;
	}
	fu_udev_device_set_dev(FU_UDEV_DEVICE(self), fu_udev_device_get_dev(bus_device));
	return TRUE;
}

static gboolean
fu_realtek_mst_device_use_drm_card(FuRealtekMstDevice *self, GError **error)
{
	g_autoptr(GUdevClient) udev_client = g_udev_client_new(NULL);
	g_autoptr(GUdevEnumerator) enumerator = g_udev_enumerator_new(udev_client);
	g_autoptr(GList) drm_devices = NULL;
	g_autoptr(FuUdevDevice) bus_device = NULL;

	/* from a drm device with the given name, find an i2c device under it
	 * and in turn an i2c-dev device representing the DPDDC bus */
	g_debug("search for DRM device with name %s", self->dp_card_kernel_name);
	g_udev_enumerator_add_match_subsystem(enumerator, "drm");
	g_udev_enumerator_add_match_name(enumerator, self->dp_card_kernel_name);
	drm_devices = g_udev_enumerator_execute(enumerator);
	for (GList *element = drm_devices; element != NULL; element = element->next) {
		g_autoptr(FuUdevDevice) drm_device = fu_udev_device_new(element->data);
		g_autoptr(GPtrArray) i2c_devices = NULL;

		if (bus_device != NULL) {
			g_debug("Ignoring additional drm device %s",
				fu_udev_device_get_sysfs_path(drm_device));
			continue;
		}

		i2c_devices = fu_udev_device_get_children_with_subsystem(drm_device, "i2c");
		bus_device = locate_i2c_bus(i2c_devices);
	}

	if (bus_device == NULL) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_NOT_FOUND,
			    "did not find an i2c-dev associated with drm device %s",
			    self->dp_card_kernel_name);
		return FALSE;
	}
	fu_udev_device_set_dev(FU_UDEV_DEVICE(self), fu_udev_device_get_dev(bus_device));
	return TRUE;
}

static gboolean
mst_ensure_device_address (FuRealtekMstDevice *self, guint8 address, GError **error)
{
	return fu_udev_device_ioctl (FU_UDEV_DEVICE (self), I2C_SLAVE,
				     (guint8 *) (guintptr) address, NULL, error);
}

/** Write a value to a device register */
static gboolean
mst_write_register (FuRealtekMstDevice *self, guint8 address, guint8 value, GError **error)
{
	const guint8 command[] = { address, value };
	return fu_i2c_device_write_full (FU_I2C_DEVICE (self), command,
					 sizeof(command), error);
}

static gboolean
mst_write_register_multi (FuRealtekMstDevice *self, guint8 address,
			  const guint8 *data, gsize count, GError **error)
{
	g_autofree guint8 *command = g_malloc0 (count + 1);
	memcpy (command + 1, data, count);
	command[0] = address;
	return fu_i2c_device_write_full (FU_I2C_DEVICE (self),
					 command, count + 1, error);

}

/** Read a register from the device */
static gboolean
mst_read_register (FuRealtekMstDevice *self,
		   guint8 address,
		   guint8 *value,
		   GError **error)
{
	if (!fu_i2c_device_write (FU_I2C_DEVICE (self), address, error))
		return FALSE;
	return fu_i2c_device_read (FU_I2C_DEVICE (self), value, error);
}

static gboolean
mst_set_indirect_address (FuRealtekMstDevice *self, guint16 address, GError **error)
{
	if (!mst_write_register (self, REG_INDIRECT_LO, 0x9F, error))
		return FALSE;
	if (!mst_write_register (self, REG_INDIRECT_HI, address >> 8, error))
		return FALSE;
	return mst_write_register (self, REG_INDIRECT_LO, address, error);
}

static gboolean
mst_read_register_indirect (FuRealtekMstDevice *self, guint16 address, guint8 *value, GError **error)
{
	if (!mst_set_indirect_address (self, address, error))
		return FALSE;
	return mst_read_register (self, REG_INDIRECT_HI, value, error);
}

static gboolean
mst_write_register_indirect (FuRealtekMstDevice *self, guint16 address, guint8 value, GError **error)
{
	if (!mst_set_indirect_address (self, address, error))
		return FALSE;
	return mst_write_register (self, REG_INDIRECT_HI, value, error);
}

/**
 * Wait until a device register reads an expected value.
 *
 * Waiting up to @timeout_seconds, poll the given @address for the read value
 * bitwise-ANDed with @mask to be equal to @expected.
 *
 * Returns an error if the timeout expires or in case of an I/O error.
 */
static gboolean
mst_poll_register (FuRealtekMstDevice *self,
		   guint8 address,
		   guint8 mask,
		   guint8 expected,
		   guint timeout_seconds,
		   GError **error)
{
	guint8 value;
	g_autoptr(GTimer) timer = g_timer_new ();

	if (!mst_read_register (self, address, &value, error))
		return FALSE;
	while ((value & mask) != expected
		&& g_timer_elapsed (timer, NULL) <= timeout_seconds) {
		g_usleep(G_TIME_SPAN_MILLISECOND);
		if (!mst_read_register (self, address, &value, error))
			return FALSE;
	}
	if ((value & mask) == expected)
		return TRUE;

	g_set_error (error, G_IO_ERROR, G_IO_ERROR_TIMED_OUT,
		     "register %x still reads %x after %us, wanted %x (mask %x)",
		     address, value, timeout_seconds, expected, mask);
	return FALSE;
}

static gboolean
mst_set_gpio88 (FuRealtekMstDevice *self, gboolean level, GError **error)
{
	guint8 value;

	/* ensure pin is configured as push-pull GPIO */
	if (!mst_read_register_indirect (self, REG_GPIO88_CONFIG, &value, error))
		return FALSE;
	if (!mst_write_register_indirect (self,
					  REG_GPIO88_CONFIG,
					  (value & 0xF0) | 1,
					  error))
		return FALSE;

	/* set output level */
	g_debug ("set pin 88 = %d", level);
	if (!mst_read_register_indirect (self, REG_GPIO88_VALUE, &value, error))
		return FALSE;
	return mst_write_register_indirect (self, REG_GPIO88_VALUE,
					    (value & 0xFE) | (level != FALSE),
					    error);
}

static gboolean
fu_realtek_mst_device_probe (FuDevice *device, GError **error)
{
	FuRealtekMstDevice *self = FU_REALTEK_MST_DEVICE (device);
	FuContext *context = fu_device_get_context (device);
	const gchar *hardware_family = NULL;
	const gchar *quirk_name = NULL;
	g_autofree gchar *family_instance_id = NULL;
	g_autofree gchar *instance_id = NULL;

	/* set custom instance ID and load matching quirks */
	instance_id = g_strdup_printf ("REALTEK-MST\\NAME_%s",
				       fu_udev_device_get_sysfs_attr (
					       FU_UDEV_DEVICE (device),
					       "name",
					       NULL));
	fu_device_add_instance_id (device, instance_id);

	hardware_family = fu_context_get_hwid_value (context, FU_HWIDS_KEY_FAMILY);
	family_instance_id = g_strdup_printf ("%s&FAMILY_%s", instance_id, hardware_family);
	fu_device_add_instance_id_full (device, family_instance_id,
					FU_DEVICE_INSTANCE_FLAG_ONLY_QUIRKS);

	/* having loaded quirks, check this device is supported */
	quirk_name = fu_device_get_name (device);
	if (g_strcmp0(quirk_name, "RTD2142") != 0 && g_strcmp0(quirk_name, "RTD2141B") != 0) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_NOT_SUPPORTED,
			    "device name %s is not supported",
			    quirk_name);
		return FALSE;
	}

	if (self->dp_aux_dev_name != NULL) {
		if (!fu_realtek_mst_device_use_aux_dev(self, error))
			return FALSE;
	} else if (self->dp_card_kernel_name != NULL) {
		if (!fu_realtek_mst_device_use_drm_card(self, error))
			return FALSE;
	} else {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_NOT_SUPPORTED,
				    "one of RealtekMstDpAuxName or RealtekMstDrmCardKernelName"
				    " must be specified");
		return FALSE;
	}

	/* locate its sibling i2c device and use that instead */

	/* FuI2cDevice */
	if (!FU_DEVICE_CLASS (fu_realtek_mst_device_parent_class)->probe (device, error))
		return FALSE;

	/* success */
	return TRUE;
}

static gboolean
fu_realtek_mst_device_get_dual_bank_info (FuRealtekMstDevice *self,
					  struct dual_bank_info *info,
					  GError **error)
{
	guint8 response[11] = { 0x0 };

	if (!mst_ensure_device_address (self, I2C_ADDR_DEBUG, error))
		return FALSE;

	/* switch to DDCCI mode */
	if (!mst_write_register (self, 0xca, 0x09, error))
		return FALSE;

	/* wait for mode switch to complete */
	g_usleep (200 * G_TIME_SPAN_MILLISECOND);

	/* request dual bank state and read back */
	if (!fu_i2c_device_write (FU_I2C_DEVICE (self), 0x01, error))
		return FALSE;
	if (!fu_i2c_device_read_full (FU_I2C_DEVICE (self), response, sizeof(response), error))
		return FALSE;

	if (response[0] != 0xca || response[1] != 9) {
		/* unexpected response code or length usually means the current
		 * firmware doesn't support dual-bank mode at all */
		g_debug ("unexpected response code %#x, length %d",
			 response[0], response[1]);
		info->is_enabled = FALSE;
		return TRUE;
	}

	/* enable flag, assume anything other than 1 is unsupported */
	if (response[2] != 1) {
		info->is_enabled = FALSE;
		return TRUE;
	}
	info->is_enabled = TRUE;

	info->mode = response[3];
	if (info->mode > DUAL_BANK_MAX_VALUE) {
		g_debug ("unexpected dual bank mode value %#x", info->mode);
		info->is_enabled = FALSE;
		return TRUE;
	}

	info->active_bank = response[4];
	if (info->active_bank > FLASH_BANK_MAX_VALUE) {
		g_debug ("unexpected active flash bank value %#x",
			 info->active_bank);
		info->is_enabled = FALSE;
		return TRUE;
	}

	info->user1_version[0] = response[5];
	info->user1_version[1] = response[6];
	info->user2_version[0] = response[7];
	info->user2_version[1] = response[8];
	/* last two bytes of response are reserved */
	return TRUE;
}

static gboolean
fu_realtek_mst_device_probe_version (FuDevice *device, GError **error)
{
	FuRealtekMstDevice *self = FU_REALTEK_MST_DEVICE (device);
	struct dual_bank_info info = { 0x0 };
	guint8 *active_version;
	g_autofree gchar *version_str = NULL;

	/* ensure probed state is cleared in case of error */
	fu_device_remove_flag (device, FWUPD_DEVICE_FLAG_UPDATABLE);
	fu_device_remove_flag (device, FWUPD_DEVICE_FLAG_DUAL_IMAGE);
	self->active_bank = FLASH_BANK_INVALID;
	fu_device_set_version (device, NULL);

	if (!fu_realtek_mst_device_get_dual_bank_info (FU_REALTEK_MST_DEVICE (self),
						       &info, error))
		return FALSE;

	if (!info.is_enabled) {
		g_debug ("dual-bank mode is not enabled");
		return TRUE;
	}
	if (info.mode != DUAL_BANK_DIFF) {
		g_debug ("can only update from dual-bank-diff mode");
		return TRUE;
	}
	/* dual-bank mode seems to be fully supported, so we can update
	 * regardless of the active bank- if it's FLASH_BANK_BOOT, updating is
	 * possible even if the current version is unknown */
	fu_device_add_flag (device, FWUPD_DEVICE_FLAG_DUAL_IMAGE);
	fu_device_add_flag (device, FWUPD_DEVICE_FLAG_UPDATABLE);

	g_debug ("device is currently running from bank %u", info.active_bank);
	g_return_val_if_fail (info.active_bank <= FLASH_BANK_MAX_VALUE, FALSE);
	self->active_bank = info.active_bank;

	g_debug ("firmware version reports user1 %d.%d, user2 %d.%d",
		 info.user1_version[0], info.user1_version[1],
		 info.user2_version[0], info.user2_version[1]);
	if (info.active_bank == FLASH_BANK_USER1)
		active_version = info.user1_version;
	else if (info.active_bank == FLASH_BANK_USER2)
		active_version = info.user2_version;
	else
		/* only user bank versions are reported, can't tell otherwise */
		return TRUE;

	version_str = g_strdup_printf ("%u.%u", active_version[0], active_version[1]);
	fu_device_set_version (FU_DEVICE (self), version_str);
	return TRUE;
}

static gboolean
flash_iface_read(FuRealtekMstDevice *self,
		 guint32 address,
		 guint8 *buf,
		 const gsize buf_size,
		 FuProgress *progress,
		 GError **error)
{
	gsize bytes_read = 0;
	guint8 byte;

	g_return_val_if_fail (address < FLASH_SIZE, FALSE);
	g_return_val_if_fail (buf_size <= FLASH_SIZE, FALSE);

	g_debug ("read %#" G_GSIZE_MODIFIER "x bytes from %#08x", buf_size, address);

	/* read must start one byte prior to the desired address and ignore the
	 * first byte of data, since the first read value is unpredictable */
	address = (address - 1) & 0xFFFFFF;
	if (!mst_write_register (self, REG_CMD_ADDR_HI, address >> 16, error))
		return FALSE;
	if (!mst_write_register (self, REG_CMD_ADDR_MID, address >> 8, error))
		return FALSE;
	if (!mst_write_register (self, REG_CMD_ADDR_LO, address, error))
		return FALSE;
	if (!mst_write_register (self, REG_READ_OPCODE, CMD_OPCODE_READ, error))
		return FALSE;

	/* ignore first byte of data */
	if (!fu_i2c_device_write (FU_I2C_DEVICE (self), 0x70, error))
		return FALSE;
	if (!fu_i2c_device_read (FU_I2C_DEVICE (self), &byte, error))
		return FALSE;

	while (bytes_read < buf_size) {
		/* read up to 256 bytes in one transaction */
		gsize read_len = buf_size - bytes_read;
		if (read_len > 256)
			read_len = 256;

		if (!fu_i2c_device_read_full (FU_I2C_DEVICE (self),
					      buf + bytes_read, read_len,
					      error))
			return FALSE;

		bytes_read += read_len;
		fu_progress_set_percentage_full(progress, bytes_read, buf_size);
	}
	return TRUE;
}

static gboolean
flash_iface_erase_sector (FuRealtekMstDevice *self, guint32 address,
			  GError **error)
{
	/* address must be 4k-aligned */
	g_return_val_if_fail ((address & 0xFFF) == 0, FALSE);
	g_debug ("sector erase %#08x-%#08x", address, address + FLASH_SECTOR_SIZE);

	/* sector address */
	if (!mst_write_register (self, REG_CMD_ADDR_HI, address >> 16, error))
		return FALSE;
	if (!mst_write_register (self, REG_CMD_ADDR_MID, address >> 8, error))
		return FALSE;
	if (!mst_write_register (self, REG_CMD_ADDR_LO, address, error))
		return FALSE;
	/* command type + WREN */
	if (!mst_write_register (self, REG_CMD_ATTR, 0xB8, error))
		return FALSE;
	/* sector erase opcode */
	if (!mst_write_register (self, REG_ERASE_OPCODE, CMD_OPCODE_ERASE_SECTOR, error))
		return FALSE;
	/* begin operation and wait for completion */
	if (!mst_write_register (self, REG_CMD_ATTR, 0xB8 | CMD_ERASE_BUSY, error))
		return FALSE;
	return mst_poll_register (self, REG_CMD_ATTR, CMD_ERASE_BUSY, 0, 10, error);
}
static gboolean
flash_iface_erase_block (FuRealtekMstDevice *self, guint32 address, GError **error)
{
	/* address must be 64k-aligned */
	g_return_val_if_fail ((address & 0xFFFF) == 0, FALSE);
	g_debug ("block erase %#08x-%#08x", address, address + FLASH_BLOCK_SIZE);

	/* block address */
	if (!mst_write_register (self, REG_CMD_ADDR_HI, address >> 16, error))
		return FALSE;
	if (!mst_write_register (self, REG_CMD_ADDR_MID, 0, error))
		return FALSE;
	if (!mst_write_register (self, REG_CMD_ADDR_LO, 0, error))
		return FALSE;
	/* command type + WREN */
	if (!mst_write_register (self, REG_CMD_ATTR, 0xB8, error))
		return FALSE;
	/* block erase opcode */
	if (!mst_write_register (self, REG_ERASE_OPCODE, CMD_OPCODE_ERASE_BLOCK, error))
		return FALSE;
	/* begin operation and wait for completion */
	if (!mst_write_register (self, REG_CMD_ATTR, 0xB8 | CMD_ERASE_BUSY, error))
		return FALSE;
	return mst_poll_register (self, REG_CMD_ATTR, CMD_ERASE_BUSY, 0, 10, error);
}

static gboolean
flash_iface_write(FuRealtekMstDevice *self,
		  guint32 address,
		  GBytes *data,
		  FuProgress *progress,
		  GError **error)
{
	gsize bytes_written = 0;
	gsize total_size = g_bytes_get_size (data);
	g_autoptr(GPtrArray) chunks = fu_chunk_array_new_from_bytes (data, address, 0, 256);

	g_debug ("write %#" G_GSIZE_MODIFIER "x bytes at %#08x", total_size, address);
	for (guint i = 0; i < chunks->len; i++) {
		FuChunk *chunk = g_ptr_array_index (chunks, i);
		guint32 chunk_address = fu_chunk_get_address (chunk);
		guint32 chunk_size = fu_chunk_get_data_sz (chunk);

		/* write opcode */
		if (!mst_write_register (self, REG_WRITE_OPCODE, CMD_OPCODE_WRITE, error))
			return FALSE;
		/* write length */
		if (!mst_write_register (self, REG_WRITE_LEN, chunk_size - 1, error))
			return FALSE;
		/* target address */
		if (!mst_write_register (self, REG_CMD_ADDR_HI, chunk_address >> 16, error))
			return FALSE;
		if (!mst_write_register (self, REG_CMD_ADDR_MID, chunk_address >> 8, error))
			return FALSE;
		if (!mst_write_register (self, REG_CMD_ADDR_LO, chunk_address, error))
			return FALSE;
		/* ensure write buffer is empty */
		if (!mst_poll_register (self, REG_MCU_MODE, MCU_MODE_WRITE_BUF, MCU_MODE_WRITE_BUF, 10, error)) {
			g_prefix_error (error, "failed waiting for write buffer to clear: ");
			return FALSE;
		}
		/* write data into FIFO */
		if (!mst_write_register_multi (self, REG_WRITE_FIFO,
					       fu_chunk_get_data (chunk),
					       chunk_size, error))
			return FALSE;
		/* begin operation and wait for completion */
		if (!mst_write_register (self, REG_MCU_MODE, MCU_MODE_ISP | MCU_MODE_WRITE_BUSY, error))
			return FALSE;
		if (!mst_poll_register (self, REG_MCU_MODE, MCU_MODE_WRITE_BUSY, 0, 10, error)) {
			g_prefix_error (error,
					"timed out waiting for write at %#x to complete: ",
					address);
			return FALSE;
		}

		bytes_written += chunk_size;
		fu_progress_set_percentage_full(progress, bytes_written, total_size);
	}

	return TRUE;
}

static gboolean
fu_realtek_mst_device_detach (FuDevice *device, GError **error)
{
	FuRealtekMstDevice *self = FU_REALTEK_MST_DEVICE (device);

	if (!mst_ensure_device_address (self, I2C_ADDR_ISP, error))
		return FALSE;

	/* Switch to programming mode (stops regular operation) */
	fu_device_set_status (device, FWUPD_STATUS_DEVICE_RESTART);
	if (!mst_write_register (self, REG_MCU_MODE, MCU_MODE_ISP, error))
		return FALSE;
	g_debug ("wait for ISP mode ready");
	if (!mst_poll_register (self, REG_MCU_MODE, MCU_MODE_ISP, MCU_MODE_ISP, 60, error))
		return FALSE;

	/* magic value makes the MCU clock run faster than normal; this both
	 * helps programming performance and fixes flakiness where register
	 * writes sometimes get nacked for no apparent reason */
	if (!mst_write_register_indirect (self, 0x06A0, 0x74, error))
		return FALSE;

	fu_device_add_flag (device, FWUPD_DEVICE_FLAG_IS_BOOTLOADER);
	fu_device_set_status (device, FWUPD_STATUS_IDLE);

	/* Disable hardware write protect, assuming Flash ~WP is connected to
	 * device pin 88, a GPIO. */
	return mst_set_gpio88 (self, 1, error);
}

static gboolean
fu_realtek_mst_device_write_firmware(FuDevice *device,
				     FuFirmware *firmware,
				     FuProgress *progress,
				     FwupdInstallFlags flags,
				     GError **error)
{
	FuRealtekMstDevice *self = FU_REALTEK_MST_DEVICE (device);
	/* write an inactive bank: USER2 if USER1 is active, otherwise USER1
	 * (including if the boot bank is active) */
	guint32 base_addr = self->active_bank == FLASH_BANK_USER1 ? FLASH_USER2_ADDR : FLASH_USER1_ADDR;
	guint32 flag_addr = self->active_bank == FLASH_BANK_USER1 ? FLASH_FLAG2_ADDR : FLASH_FLAG1_ADDR;
	GBytes *firmware_bytes = fu_firmware_get_bytes (firmware, error);
	const guint8 flag_data[] = {0xaa, 0xaa, 0xaa, 0xff, 0xff};
	g_autofree guint8 *readback_buf = g_malloc0 (FLASH_USER_SIZE);

	g_return_val_if_fail (g_bytes_get_size (firmware_bytes) == FLASH_USER_SIZE, FALSE);

	if (!mst_ensure_device_address (self, I2C_ADDR_ISP, error))
		return FALSE;

	/* erase old image */
	g_debug ("erase old image from %#x", base_addr);
	fu_device_set_status (device, FWUPD_STATUS_DEVICE_ERASE);
	for (guint32 offset = 0; offset < FLASH_USER_SIZE; offset += FLASH_BLOCK_SIZE) {
		fu_progress_set_percentage_full(progress, offset, FLASH_USER_SIZE);
		if (!flash_iface_erase_block (self, base_addr + offset, error))
			return FALSE;
	}

	/* write new image */
	g_debug ("write new image to %#x", base_addr);
	fu_device_set_status (device, FWUPD_STATUS_DEVICE_WRITE);
	if (!flash_iface_write(self, base_addr, firmware_bytes, progress, error))
		return FALSE;

	fu_device_set_status (device, FWUPD_STATUS_DEVICE_VERIFY);
	if (!flash_iface_read(self, base_addr, readback_buf, FLASH_USER_SIZE, progress, error))
		return FALSE;
	if (memcmp (g_bytes_get_data (firmware_bytes, NULL), readback_buf, FLASH_USER_SIZE) != 0) {
		g_set_error (error, FWUPD_ERROR, FWUPD_ERROR_WRITE,
			     "flash contents after write do not match firmware image");
		return FALSE;
	}

	/* Erase old flag and write new one. The MST appears to modify the
	 * flag value once booted, so we always write the same value here and
	 * it picks up what we've updated. */
	fu_device_set_status (device, FWUPD_STATUS_DEVICE_ERASE);
	if (!flash_iface_erase_sector (self, flag_addr & ~(FLASH_SECTOR_SIZE - 1), error))
		return FALSE;
	fu_device_set_status (device, FWUPD_STATUS_DEVICE_WRITE);
	return flash_iface_write(self,
				 flag_addr,
				 g_bytes_new_static(flag_data, sizeof(flag_data)),
				 progress,
				 error);
}

static FuFirmware *
fu_realtek_mst_device_read_firmware(FuDevice *device, FuProgress *progress, GError **error)
{
	FuRealtekMstDevice *self = FU_REALTEK_MST_DEVICE (device);
	guint32 bank_address;
	g_autofree guint8 *image_bytes = NULL;

	if (self->active_bank == FLASH_BANK_USER1)
		bank_address = FLASH_USER1_ADDR;
	else if (self->active_bank == FLASH_BANK_USER2)
		bank_address = FLASH_USER2_ADDR;
	else {
		g_set_error (error, FWUPD_ERROR, FWUPD_ERROR_NOT_SUPPORTED,
			     "cannot read firmware from bank %u",
			     self->active_bank);
		return NULL;
	}

	image_bytes = g_malloc0 (FLASH_USER_SIZE);
	if (!mst_ensure_device_address (self, I2C_ADDR_ISP, error))
		return NULL;
	if (!flash_iface_read(self, bank_address, image_bytes, FLASH_USER_SIZE, progress, error))
		return NULL;
	return fu_firmware_new_from_bytes(g_bytes_new_take (g_steal_pointer (&image_bytes), FLASH_USER_SIZE));
}

static GBytes *
fu_realtek_mst_device_dump_firmware(FuDevice *device, FuProgress *progress, GError **error)
{
	FuRealtekMstDevice *self = FU_REALTEK_MST_DEVICE (device);
	g_autofree guint8 *flash_contents = g_malloc0 (FLASH_SIZE);

	if (!mst_ensure_device_address (self, I2C_ADDR_ISP, error))
		return NULL;
	fu_device_set_status (device, FWUPD_STATUS_DEVICE_READ);
	if (!flash_iface_read(self, 0, flash_contents, FLASH_SIZE, progress, error))
		return NULL;
	fu_device_set_status (device, FWUPD_STATUS_IDLE);

	return g_bytes_new_take (g_steal_pointer (&flash_contents), FLASH_SIZE);
}

static gboolean
fu_realtek_mst_device_attach (FuDevice *device, GError **error)
{
	FuRealtekMstDevice *self = FU_REALTEK_MST_DEVICE (device);
	guint8 value;

	if (!mst_ensure_device_address (self, I2C_ADDR_ISP, error))
		return FALSE;

	/* re-enable hardware write protect via GPIO */
	if (!mst_set_gpio88 (self, 0, error))
		return FALSE;

	if (!mst_read_register (self, REG_MCU_MODE, &value, error))
		return FALSE;
	if ((value & MCU_MODE_ISP) != 0) {
		g_autoptr(GError) error_local = NULL;

		g_debug ("resetting device to exit ISP mode");
		fu_device_set_status (device, FWUPD_STATUS_DEVICE_RESTART);

		/* Set register EE bit 2 to request reset. This write can fail
		 * spuriously, so we ignore the write result and verify the device is
		 * no longer in programming mode after giving it time to reset. */
		if (!mst_read_register (self, 0xEE, &value, error))
			return FALSE;
		if (!mst_write_register (self, 0xEE, value | 2, &error_local)) {
			g_debug ("write spuriously failed, ignoring: %s",
				 error_local->message);
		}

		/* allow device some time to reset */
		g_usleep (G_USEC_PER_SEC);

		/* verify device has exited programming mode and actually reset */
		if (!mst_read_register (self, REG_MCU_MODE, &value, error))
			return FALSE;
		if ((value & MCU_MODE_ISP) == MCU_MODE_ISP) {
			g_set_error_literal (error,
					     FWUPD_ERROR,
					     FWUPD_ERROR_NEEDS_USER_ACTION,
					     "device failed to reset when requested");
			fu_device_add_flag (device, FWUPD_DEVICE_FLAG_NEEDS_SHUTDOWN);
			return FALSE;
		}
	} else {
		g_debug ("device is already in normal mode");
	}

	fu_device_remove_flag (device, FWUPD_DEVICE_FLAG_IS_BOOTLOADER);
	fu_device_set_status (device, FWUPD_STATUS_IDLE);
	return TRUE;
}

static void
fu_realtek_mst_device_init (FuRealtekMstDevice *self)
{
	self->active_bank = FLASH_BANK_INVALID;

	fu_device_add_flag (FU_DEVICE (self), FWUPD_DEVICE_FLAG_INTERNAL);
	fu_device_set_version_format (FU_DEVICE (self), FWUPD_VERSION_FORMAT_PAIR);
	fu_device_add_flag (FU_DEVICE (self), FWUPD_DEVICE_FLAG_CAN_VERIFY_IMAGE);
	fu_device_add_protocol (FU_DEVICE (self), "com.realtek.rtd2142");
	fu_device_set_vendor (FU_DEVICE (self), "Realtek");
	fu_device_add_vendor_id (FU_DEVICE (self), "PCI:0x10EC");
	fu_device_set_summary (FU_DEVICE (self), "DisplayPort MST hub");
	fu_device_add_icon (FU_DEVICE (self), "video-display");
	fu_device_set_firmware_size (FU_DEVICE (self), FLASH_USER_SIZE);
}

static void
fu_realtek_mst_device_finalize (GObject *object)
{
	FuRealtekMstDevice *self = FU_REALTEK_MST_DEVICE (object);
	g_free (self->dp_aux_dev_name);
	g_free(self->dp_card_kernel_name);
	G_OBJECT_CLASS (fu_realtek_mst_device_parent_class)->finalize (object);
}

static void
fu_realtek_mst_device_class_init (FuRealtekMstDeviceClass *klass)
{
	FuDeviceClass *klass_device = FU_DEVICE_CLASS (klass);
	GObjectClass *klass_object = G_OBJECT_CLASS(klass);

	klass_object->finalize = fu_realtek_mst_device_finalize;
	klass_device->probe = fu_realtek_mst_device_probe;
	klass_device->set_quirk_kv = fu_realtek_mst_device_set_quirk_kv;
	klass_device->setup = fu_realtek_mst_device_probe_version;
	klass_device->detach = fu_realtek_mst_device_detach;
	klass_device->attach = fu_realtek_mst_device_attach;
	klass_device->write_firmware = fu_realtek_mst_device_write_firmware;
	klass_device->reload = fu_realtek_mst_device_probe_version;
	/* read active image */
	klass_device->read_firmware = fu_realtek_mst_device_read_firmware;
	/* dump whole flash */
	klass_device->dump_firmware = fu_realtek_mst_device_dump_firmware;
}
