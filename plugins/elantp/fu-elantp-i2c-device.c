/*
 * Copyright (C) 2020 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include <fwupdplugin.h>

#include <fcntl.h>
#include <linux/i2c-dev.h>

#include "fu-elantp-common.h"
#include "fu-elantp-firmware.h"
#include "fu-elantp-i2c-device.h"

struct _FuElantpI2cDevice {
	FuUdevDevice parent_instance;
	guint16 i2c_addr;
	guint16 ic_page_count;
	guint16 iap_type;
	guint16 iap_ctrl;
	guint16 iap_password;
	guint16 module_id;
	guint16 fw_page_size;
	guint8 pattern;
	gchar *bind_path;
	gchar *bind_id;
};

G_DEFINE_TYPE(FuElantpI2cDevice, fu_elantp_i2c_device, FU_TYPE_UDEV_DEVICE)

#define FU_ELANTP_DEVICE_IOCTL_TIMEOUT 5000 /* ms */

static gboolean
fu_elantp_i2c_device_detach(FuDevice *device, FuProgress *progress, GError **error);

static void
fu_elantp_i2c_device_to_string(FuDevice *device, guint idt, GString *str)
{
	FuElantpI2cDevice *self = FU_ELANTP_I2C_DEVICE(device);
	fu_string_append_kx(str, idt, "I2cAddr", self->i2c_addr);
	fu_string_append_kx(str, idt, "ModuleId", self->module_id);
	fu_string_append_kx(str, idt, "Pattern", self->pattern);
	fu_string_append_kx(str, idt, "FwPageSize", self->fw_page_size);
	fu_string_append_kx(str, idt, "IcPageCount", self->ic_page_count);
	fu_string_append_kx(str, idt, "IapType", self->iap_type);
	fu_string_append_kx(str, idt, "IapCtrl", self->iap_ctrl);
	fu_string_append(str, idt, "BindPath", self->bind_path);
	fu_string_append(str, idt, "BindId", self->bind_id);
}

static gboolean
fu_elantp_i2c_device_writeln(const gchar *fn, const gchar *buf, GError **error)
{
	int fd;
	g_autoptr(FuIOChannel) io = NULL;

	if (!g_file_test(fn, G_FILE_TEST_EXISTS)) {
		g_set_error(error, FWUPD_ERROR, FWUPD_ERROR_INVALID_FILE, "%s does not exist", fn);
		return FALSE;
	}

	fd = open(fn, O_WRONLY);
	if (fd < 0) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_PERMISSION_DENIED,
			    "could not open %s",
			    fn);
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
fu_elantp_i2c_device_rebind_driver(FuElantpI2cDevice *self, GError **error)
{
	if (self->bind_path == NULL || self->bind_id == NULL) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INVALID_FILE,
			    "no Path or ID for rebind driver");
		return FALSE;
	}

	if (!fu_elantp_i2c_device_writeln(g_build_filename(self->bind_path, "unbind", NULL),
					  self->bind_id,
					  error))
		return FALSE;
	if (!fu_elantp_i2c_device_writeln(g_build_filename(self->bind_path, "bind", NULL),
					  self->bind_id,
					  error))
		return FALSE;

	g_debug("rebind driver of %s", self->bind_id);
	return TRUE;
}

static gboolean
fu_elantp_i2c_device_probe(FuDevice *device, GError **error)
{
	FuElantpI2cDevice *self = FU_ELANTP_I2C_DEVICE(device);

	/* check is valid */
	if (g_strcmp0(fu_udev_device_get_subsystem(FU_UDEV_DEVICE(device)), "i2c") == 0) {
		g_autoptr(GPtrArray) i2c_buses = NULL;
		FuUdevDevice *i2c_device =
		    fu_udev_device_get_parent_with_subsystem(FU_UDEV_DEVICE(device), "i2c");
		if (i2c_device == NULL) {
			g_set_error(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_NOT_SUPPORTED,
				    "did not find the i2c parent for device");
			return FALSE;
		}

		i2c_buses = fu_udev_device_get_children_with_subsystem(i2c_device, "i2c-dev");
		if (i2c_buses->len == 1) {
			FuUdevDevice *bus_device = g_object_ref(g_ptr_array_index(i2c_buses, 0));
			if (bus_device == NULL) {
				g_set_error(error,
					    FWUPD_ERROR,
					    FWUPD_ERROR_NOT_SUPPORTED,
					    "did not find the i2c-dev children for device");
				return FALSE;
			}

			g_debug("Found I2C bus at %s, using this device",
				fu_udev_device_get_sysfs_path(bus_device));
			self->bind_path =
			    g_build_filename("/sys/bus/i2c/drivers",
					     fu_udev_device_get_driver(FU_UDEV_DEVICE(device)),
					     NULL);
			self->bind_id = g_path_get_basename(
			    fu_udev_device_get_sysfs_path(FU_UDEV_DEVICE(device)));
			fu_udev_device_set_dev(FU_UDEV_DEVICE(device),
					       fu_udev_device_get_dev(bus_device));
		}
	}

	if (g_strcmp0(fu_udev_device_get_subsystem(FU_UDEV_DEVICE(device)), "i2c-dev") != 0) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_NOT_SUPPORTED,
			    "is not correct subsystem=%s, expected i2c-dev",
			    fu_udev_device_get_subsystem(FU_UDEV_DEVICE(device)));
		return FALSE;
	}
	if (fu_udev_device_get_device_file(FU_UDEV_DEVICE(device)) == NULL) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_NOT_SUPPORTED,
				    "no device file");
		return FALSE;
	}

	/* set the physical ID */
	return fu_udev_device_set_physical_id(FU_UDEV_DEVICE(device), "i2c", error);
}

static gboolean
fu_elantp_i2c_device_send_cmd(FuElantpI2cDevice *self,
			      guint8 *tx,
			      gssize txsz,
			      guint8 *rx,
			      gssize rxsz,
			      GError **error)
{
	if (g_getenv("FWUPD_ELANTP_VERBOSE") != NULL)
		fu_dump_raw(G_LOG_DOMAIN, "Write", tx, txsz);
	if (!fu_udev_device_pwrite(FU_UDEV_DEVICE(self), 0, tx, txsz, error))
		return FALSE;
	if (rxsz == 0)
		return TRUE;
	if (!fu_udev_device_pread(FU_UDEV_DEVICE(self), 0, rx, rxsz, error))
		return FALSE;
	if (g_getenv("FWUPD_ELANTP_VERBOSE") != NULL)
		fu_dump_raw(G_LOG_DOMAIN, "Read", rx, rxsz);
	return TRUE;
}

static gboolean
fu_elantp_i2c_device_write_cmd(FuElantpI2cDevice *self, guint16 reg, guint16 cmd, GError **error)
{
	guint8 buf[4];
	fu_memwrite_uint16(buf + 0x0, reg, G_LITTLE_ENDIAN);
	fu_memwrite_uint16(buf + 0x2, cmd, G_LITTLE_ENDIAN);
	return fu_elantp_i2c_device_send_cmd(self, buf, sizeof(buf), NULL, 0, error);
}

static gboolean
fu_elantp_i2c_device_read_cmd(FuElantpI2cDevice *self,
			      guint16 reg,
			      guint8 *rx,
			      gsize rxsz,
			      GError **error)
{
	guint8 buf[2];
	fu_memwrite_uint16(buf + 0x0, reg, G_LITTLE_ENDIAN);
	return fu_elantp_i2c_device_send_cmd(self, buf, sizeof(buf), rx, rxsz, error);
}

static gboolean
fu_elantp_i2c_device_ensure_iap_ctrl(FuElantpI2cDevice *self, GError **error)
{
	guint8 buf[2] = {0x0};
	if (!fu_elantp_i2c_device_read_cmd(self, ETP_CMD_I2C_IAP_CTRL, buf, sizeof(buf), error)) {
		g_prefix_error(error, "failed to read IAPControl: ");
		return FALSE;
	}
	if (!fu_memread_uint16_safe(buf, sizeof(buf), 0x0, &self->iap_ctrl, G_LITTLE_ENDIAN, error))
		return FALSE;

	/* in bootloader mode? */
	if ((self->iap_ctrl & ETP_I2C_MAIN_MODE_ON) == 0)
		fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_IS_BOOTLOADER);
	else
		fu_device_remove_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_IS_BOOTLOADER);

	return TRUE;
}

static gboolean
fu_elantp_i2c_device_setup(FuDevice *device, GError **error)
{
	FuElantpI2cDevice *self = FU_ELANTP_I2C_DEVICE(device);
	guint16 fwver;
	guint16 iap_ver;
	guint16 tmp;
	guint16 pid;
	guint16 vid;
	guint8 buf[30] = {0x0};
	guint8 ic_type;
	g_autofree gchar *version_bl = NULL;
	g_autofree gchar *version = NULL;

	/* read the I2C descriptor */
	if (!fu_elantp_i2c_device_read_cmd(self,
					   ETP_CMD_GET_HID_DESCRIPTOR,
					   buf,
					   sizeof(buf),
					   error)) {
		g_prefix_error(error, "failed to get HID descriptor: ");
		return FALSE;
	}
	if (!fu_memread_uint16_safe(buf, sizeof(buf), 20, &vid, G_LITTLE_ENDIAN, error))
		return FALSE;
	if (!fu_memread_uint16_safe(buf, sizeof(buf), 22, &pid, G_LITTLE_ENDIAN, error))
		return FALSE;

	/* set the vendor ID */
	if (vid != 0x0000) {
		g_autofree gchar *vendor_id = NULL;
		vendor_id = g_strdup_printf("HIDRAW:0x%04X", vid);
		fu_device_add_vendor_id(device, vendor_id);
	}

	/* add GUIDs in order of priority */
	fu_device_add_instance_u16(device, "VID", vid);
	fu_device_add_instance_u16(device, "PID", pid);
	if (!fu_device_build_instance_id(device, error, "HIDRAW", "VID", "PID", NULL))
		return FALSE;

	/* get pattern */
	if (!fu_elantp_i2c_device_read_cmd(self, ETP_CMD_I2C_GET_HID_ID, buf, sizeof(buf), error)) {
		g_prefix_error(error, "failed to read I2C ID: ");
		return FALSE;
	}
	if (!fu_memread_uint16_safe(buf, sizeof(buf), 0x0, &tmp, G_LITTLE_ENDIAN, error))
		return FALSE;
	self->pattern = tmp != 0xffff ? (tmp & 0xff00) >> 8 : 0;

	/* get current firmware version */
	if (!fu_elantp_i2c_device_read_cmd(self, ETP_CMD_I2C_FW_VERSION, buf, sizeof(buf), error)) {
		g_prefix_error(error, "failed to read fw version: ");
		return FALSE;
	}
	if (!fu_memread_uint16_safe(buf, sizeof(buf), 0x0, &fwver, G_LITTLE_ENDIAN, error))
		return FALSE;
	if (fwver == 0xFFFF || fwver == ETP_CMD_I2C_FW_VERSION)
		fwver = 0;
	version = fu_version_from_uint16(fwver, FWUPD_VERSION_FORMAT_HEX);
	fu_device_set_version(device, version);

	/* get IAP firmware version */
	if (!fu_elantp_i2c_device_read_cmd(self,
					   self->pattern == 0 ? ETP_CMD_I2C_IAP_VERSION
							      : ETP_CMD_I2C_IAP_VERSION_2,
					   buf,
					   sizeof(buf),
					   error)) {
		g_prefix_error(error, "failed to read bootloader version: ");
		return FALSE;
	}
	if (self->pattern >= 1) {
		iap_ver = buf[1];
	} else {
		if (!fu_memread_uint16_safe(buf,
					    sizeof(buf),
					    0x0,
					    &iap_ver,
					    G_LITTLE_ENDIAN,
					    error))
			return FALSE;
	}
	version_bl = fu_version_from_uint16(iap_ver, FWUPD_VERSION_FORMAT_HEX);
	fu_device_set_version_bootloader(device, version_bl);

	/* get module ID */
	if (!fu_elantp_i2c_device_read_cmd(self, ETP_CMD_GET_MODULE_ID, buf, sizeof(buf), error)) {
		g_prefix_error(error, "failed to read module ID: ");
		return FALSE;
	}
	if (!fu_memread_uint16_safe(buf,
				    sizeof(buf),
				    0x0,
				    &self->module_id,
				    G_LITTLE_ENDIAN,
				    error))
		return FALSE;

	/* define the extra instance IDs */
	fu_device_add_instance_u16(device, "VEN", vid);
	fu_device_add_instance_u16(device, "DEV", pid);
	fu_device_add_instance_u16(device, "MOD", self->module_id);
	if (!fu_device_build_instance_id(device, error, "HIDRAW", "VEN", "DEV", "MOD", NULL))
		return FALSE;

	/* get OSM version */
	if (!fu_elantp_i2c_device_read_cmd(self,
					   ETP_CMD_I2C_OSM_VERSION,
					   buf,
					   sizeof(buf),
					   error)) {
		g_prefix_error(error, "failed to read OSM version: ");
		return FALSE;
	}
	if (!fu_memread_uint16_safe(buf, sizeof(buf), 0x0, &tmp, G_LITTLE_ENDIAN, error))
		return FALSE;
	if (tmp == ETP_CMD_I2C_OSM_VERSION || tmp == 0xFFFF) {
		if (!fu_elantp_i2c_device_read_cmd(self,
						   ETP_CMD_I2C_IAP_ICBODY,
						   buf,
						   sizeof(buf),
						   error)) {
			g_prefix_error(error, "failed to read IC body: ");
			return FALSE;
		}
		if (!fu_memread_uint16_safe(buf, sizeof(buf), 0x0, &tmp, G_LITTLE_ENDIAN, error))
			return FALSE;
		ic_type = tmp & 0xFF;
	} else {
		ic_type = (tmp >> 8) & 0xFF;
	}

	/* define the extra instance IDs (ic_type + module_id + driver) */
	fu_device_add_instance_u8(device, "ICTYPE", ic_type);
	fu_device_build_instance_id(device, NULL, "ELANTP", "ICTYPE", NULL);
	fu_device_build_instance_id(device, NULL, "ELANTP", "ICTYPE", "MOD", NULL);
	if (fu_device_has_private_flag(device, FU_ELANTP_I2C_DEVICE_ABSOLUTE)) {
		fu_device_add_instance_str(device, "DRIVER", "ELAN_I2C");
	} else {
		fu_device_add_flag(device, FWUPD_DEVICE_FLAG_NEEDS_REBOOT);
		fu_device_add_instance_str(device, "DRIVER", "HID");
	}
	fu_device_build_instance_id(device, NULL, "ELANTP", "ICTYPE", "MOD", "DRIVER", NULL);

	/* no quirk entry */
	if (self->ic_page_count == 0x0) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_NOT_SUPPORTED,
			    "no page count for ELANTP\\ICTYPE_%02X",
			    ic_type);
		return FALSE;
	}
	fu_device_set_firmware_size(device, (guint64)self->ic_page_count * (guint64)64);

	/* is in bootloader mode */
	if (!fu_elantp_i2c_device_ensure_iap_ctrl(self, error))
		return FALSE;

	/* success */
	return TRUE;
}

static gboolean
fu_elantp_i2c_device_open(FuDevice *device, GError **error)
{
	FuElantpI2cDevice *self = FU_ELANTP_I2C_DEVICE(device);
	gint addr = self->i2c_addr;
	guint8 tx_buf[] = {0x02, 0x01};

	/* FuUdevDevice->open */
	if (!FU_DEVICE_CLASS(fu_elantp_i2c_device_parent_class)->open(device, error))
		return FALSE;

	/* set target address */
	if (!fu_udev_device_ioctl(FU_UDEV_DEVICE(device),
				  I2C_SLAVE,
				  GINT_TO_POINTER(addr),
				  NULL,
				  FU_ELANTP_DEVICE_IOCTL_TIMEOUT,
				  NULL)) {
		if (!fu_udev_device_ioctl(FU_UDEV_DEVICE(device),
					  I2C_SLAVE_FORCE,
					  GINT_TO_POINTER(addr),
					  NULL,
					  FU_ELANTP_DEVICE_IOCTL_TIMEOUT,
					  error)) {
			g_prefix_error(error,
				       "failed to set target address to 0x%x: ",
				       self->i2c_addr);
			return FALSE;
		}
	}

	/* read i2c device */
	return fu_udev_device_pwrite(FU_UDEV_DEVICE(device), 0x0, tx_buf, sizeof(tx_buf), error);
}

static FuFirmware *
fu_elantp_i2c_device_prepare_firmware(FuDevice *device,
				      GBytes *fw,
				      FwupdInstallFlags flags,
				      GError **error)
{
	FuElantpI2cDevice *self = FU_ELANTP_I2C_DEVICE(device);
	guint16 module_id;
	g_autoptr(FuFirmware) firmware = fu_elantp_firmware_new();

	/* check is compatible with hardware */
	if (!fu_firmware_parse(firmware, fw, flags, error))
		return NULL;
	module_id = fu_elantp_firmware_get_module_id(FU_ELANTP_FIRMWARE(firmware));
	if (self->module_id != module_id) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INVALID_FILE,
			    "firmware incompatible, got 0x%04x, expected 0x%04x",
			    module_id,
			    self->module_id);
		return NULL;
	}

	/* success */
	return g_steal_pointer(&firmware);
}

static gboolean
fu_elantp_i2c_device_write_firmware(FuDevice *device,
				    FuFirmware *firmware,
				    FuProgress *progress,
				    FwupdInstallFlags flags,
				    GError **error)
{
	FuElantpI2cDevice *self = FU_ELANTP_I2C_DEVICE(device);
	FuElantpFirmware *firmware_elantp = FU_ELANTP_FIRMWARE(firmware);
	gsize bufsz = 0;
	guint16 checksum = 0;
	guint16 checksum_device = 0;
	guint16 iap_addr;
	const guint8 *buf;
	guint8 csum_buf[2] = {0x0};
	g_autoptr(GBytes) fw = NULL;
	g_autoptr(GPtrArray) chunks = NULL;

	/* progress */
	fu_progress_set_id(progress, G_STRLOC);
	fu_progress_add_flag(progress, FU_PROGRESS_FLAG_GUESSED);
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_BUSY, 2, NULL);
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_WRITE, 90, NULL);
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_VERIFY, 10, NULL);
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_RESTART, 1, NULL);

	/* simple image */
	fw = fu_firmware_get_bytes(firmware, error);
	if (fw == NULL)
		return FALSE;

	/* detach */
	if (!fu_elantp_i2c_device_detach(device, fu_progress_get_child(progress), error))
		return FALSE;
	fu_progress_step_done(progress);

	/* write each block */
	buf = g_bytes_get_data(fw, &bufsz);
	iap_addr = fu_elantp_firmware_get_iap_addr(firmware_elantp);
	chunks = fu_chunk_array_new(buf + iap_addr, bufsz - iap_addr, 0x0, 0x0, self->fw_page_size);
	for (guint i = 0; i < chunks->len; i++) {
		FuChunk *chk = g_ptr_array_index(chunks, i);
		guint16 csum_tmp =
		    fu_sum16w(fu_chunk_get_data(chk), fu_chunk_get_data_sz(chk), G_LITTLE_ENDIAN);
		gsize blksz = self->fw_page_size + 4;
		g_autofree guint8 *blk = g_malloc0(blksz);

		/* write block */
		blk[0] = ETP_I2C_IAP_REG_L;
		blk[1] = ETP_I2C_IAP_REG_H;
		if (!fu_memcpy_safe(blk,
				    blksz,
				    0x2, /* dst */
				    fu_chunk_get_data(chk),
				    fu_chunk_get_data_sz(chk),
				    0x0, /* src */
				    fu_chunk_get_data_sz(chk),
				    error))
			return FALSE;

		fu_memwrite_uint16(blk + fu_chunk_get_data_sz(chk) + 2, csum_tmp, G_LITTLE_ENDIAN);

		if (!fu_elantp_i2c_device_send_cmd(self, blk, blksz, NULL, 0, error))
			return FALSE;
		g_usleep(self->fw_page_size == 512 ? ELANTP_DELAY_WRITE_BLOCK_512 * 1000
						   : ELANTP_DELAY_WRITE_BLOCK * 1000);

		if (!fu_elantp_i2c_device_ensure_iap_ctrl(self, error))
			return FALSE;
		if (self->iap_ctrl & (ETP_FW_IAP_PAGE_ERR | ETP_FW_IAP_INTF_ERR)) {
			g_set_error(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_WRITE,
				    "bootloader reports failed write: 0x%x",
				    self->iap_ctrl);
			return FALSE;
		}

		/* update progress */
		checksum += csum_tmp;
		fu_progress_set_percentage_full(fu_progress_get_child(progress),
						(gsize)i + 1,
						(gsize)chunks->len);
	}
	fu_progress_step_done(progress);

	/* verify the written checksum */
	if (!fu_elantp_i2c_device_read_cmd(self,
					   ETP_CMD_I2C_IAP_CHECKSUM,
					   csum_buf,
					   sizeof(csum_buf),
					   error))
		return FALSE;
	if (!fu_memread_uint16_safe(csum_buf,
				    sizeof(csum_buf),
				    0x0,
				    &checksum_device,
				    G_LITTLE_ENDIAN,
				    error))
		return FALSE;
	if (checksum != checksum_device) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_WRITE,
			    "checksum failed 0x%04x != 0x%04x",
			    checksum,
			    checksum_device);
		return FALSE;
	}
	fu_progress_step_done(progress);

	/* wait for a reset */
	fu_progress_sleep(fu_progress_get_child(progress), ELANTP_DELAY_COMPLETE);
	fu_progress_step_done(progress);
	return TRUE;
}

static gboolean
fu_elantp_i2c_device_detach(FuDevice *device, FuProgress *progress, GError **error)
{
	guint16 iap_ver;
	guint16 ic_type;
	guint8 buf[2] = {0x0};
	guint16 tmp;
	FuElantpI2cDevice *self = FU_ELANTP_I2C_DEVICE(device);

	/* sanity check */
	if (fu_device_has_flag(device, FWUPD_DEVICE_FLAG_IS_BOOTLOADER)) {
		g_debug("in bootloader mode, reset IC");
		if (!fu_elantp_i2c_device_write_cmd(self,
						    ETP_CMD_I2C_IAP_RESET,
						    ETP_I2C_IAP_RESET,
						    error))
			return FALSE;
		g_usleep(ELANTP_DELAY_RESET * 1000);
	}
	/* get OSM version */
	if (!fu_elantp_i2c_device_read_cmd(self,
					   ETP_CMD_I2C_OSM_VERSION,
					   buf,
					   sizeof(buf),
					   error)) {
		g_prefix_error(error, "failed to read OSM version: ");
		return FALSE;
	}
	if (!fu_memread_uint16_safe(buf, sizeof(buf), 0x0, &tmp, G_LITTLE_ENDIAN, error))
		return FALSE;
	if (tmp == ETP_CMD_I2C_OSM_VERSION || tmp == 0xFFFF) {
		if (!fu_elantp_i2c_device_read_cmd(self,
						   ETP_CMD_I2C_IAP_ICBODY,
						   buf,
						   sizeof(buf),
						   error)) {
			g_prefix_error(error, "failed to read IC body: ");
			return FALSE;
		}
		if (!fu_memread_uint16_safe(buf,
					    sizeof(buf),
					    0x0,
					    &ic_type,
					    G_LITTLE_ENDIAN,
					    error))
			return FALSE;
	} else {
		ic_type = (tmp >> 8) & 0xFF;
	}

	/* get IAP firmware version */
	if (!fu_elantp_i2c_device_read_cmd(self,
					   self->pattern == 0 ? ETP_CMD_I2C_IAP_VERSION
							      : ETP_CMD_I2C_IAP_VERSION_2,
					   buf,
					   sizeof(buf),
					   error)) {
		g_prefix_error(error, "failed to read bootloader version: ");
		return FALSE;
	}
	if (self->pattern >= 1) {
		iap_ver = buf[1];
	} else {
		if (!fu_memread_uint16_safe(buf,
					    sizeof(buf),
					    0x0,
					    &iap_ver,
					    G_LITTLE_ENDIAN,
					    error))
			return FALSE;
	}

	/* set the page size */
	self->fw_page_size = 64;
	if (ic_type >= 0x10) {
		if (iap_ver >= 1) {
			if (iap_ver >= 2 && (ic_type == 0x14 || ic_type == 0x15)) {
				self->fw_page_size = 512;
			} else {
				self->fw_page_size = 128;
			}
			/* set the IAP type, presumably some kind of ABI */
			if (!fu_elantp_i2c_device_write_cmd(self,
							    ETP_CMD_I2C_IAP_TYPE,
							    self->fw_page_size / 2,
							    error))
				return FALSE;
			if (!fu_elantp_i2c_device_read_cmd(self,
							   ETP_CMD_I2C_IAP_TYPE,
							   buf,
							   sizeof(buf),
							   error)) {
				g_prefix_error(error, "failed to read IAP type: ");
				return FALSE;
			}
			if (!fu_memread_uint16_safe(buf,
						    sizeof(buf),
						    0x0,
						    &self->iap_type,
						    G_LITTLE_ENDIAN,
						    error))
				return FALSE;
			if (self->iap_type != self->fw_page_size / 2) {
				g_set_error_literal(error,
						    FWUPD_ERROR,
						    FWUPD_ERROR_NOT_SUPPORTED,
						    "failed to set IAP type");
				return FALSE;
			}
		}
	}
	if (!fu_elantp_i2c_device_write_cmd(self, ETP_CMD_I2C_IAP, self->iap_password, error))
		return FALSE;
	g_usleep(ELANTP_DELAY_UNLOCK * 1000);
	if (!fu_elantp_i2c_device_ensure_iap_ctrl(self, error))
		return FALSE;
	if ((self->iap_ctrl & ETP_FW_IAP_CHECK_PW) == 0) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_WRITE,
				    "unexpected bootloader password");
		return FALSE;
	}

	/* success */
	return TRUE;
}

static gboolean
fu_elantp_i2c_device_attach(FuDevice *device, FuProgress *progress, GError **error)
{
	FuElantpI2cDevice *self = FU_ELANTP_I2C_DEVICE(device);

	/* sanity check */
	if (!fu_device_has_flag(device, FWUPD_DEVICE_FLAG_IS_BOOTLOADER)) {
		g_debug("already in runtime mode, skipping");
		return TRUE;
	}

	/* reset back to runtime */
	if (!fu_elantp_i2c_device_write_cmd(self, ETP_CMD_I2C_IAP_RESET, ETP_I2C_IAP_RESET, error))
		return FALSE;
	g_usleep(ELANTP_DELAY_RESET * 1000);
	if (!fu_elantp_i2c_device_write_cmd(self,
					    ETP_CMD_I2C_IAP_RESET,
					    ETP_I2C_ENABLE_REPORT,
					    error)) {
		g_prefix_error(error, "cannot enable TP report: ");
		return FALSE;
	}

	if (!fu_elantp_i2c_device_ensure_iap_ctrl(self, error))
		return FALSE;

	if (fu_device_has_private_flag(device, FU_ELANTP_I2C_DEVICE_ABSOLUTE)) {
		g_autoptr(GError) error_local = NULL;

		if (!fu_elantp_i2c_device_write_cmd(self, 0x0300, 0x001, error)) {
			g_prefix_error(error, "cannot switch to TP ABS mode: ");
			return FALSE;
		}

		if (!fu_elantp_i2c_device_rebind_driver(self, &error_local)) {
			if (g_error_matches(error_local,
					    FWUPD_ERROR,
					    FWUPD_ERROR_PERMISSION_DENIED)) {
				g_debug("%s", error_local->message);
			} else {
				g_propagate_error(error, g_steal_pointer(&error_local));
				return FALSE;
			}
		}
	} else {
		if (!fu_elantp_i2c_device_write_cmd(self, 0x0306, 0x003, error)) {
			g_prefix_error(error, "cannot switch to TP PTP mode: ");
			return FALSE;
		}
	}

	/* success */
	return TRUE;
}

static gboolean
fu_elantp_i2c_device_set_quirk_kv(FuDevice *device,
				  const gchar *key,
				  const gchar *value,
				  GError **error)
{
	FuElantpI2cDevice *self = FU_ELANTP_I2C_DEVICE(device);
	guint64 tmp = 0;

	if (g_strcmp0(key, "ElantpIcPageCount") == 0) {
		if (!fu_strtoull(value, &tmp, 0, G_MAXUINT16, error))
			return FALSE;
		self->ic_page_count = (guint16)tmp;
		return TRUE;
	}
	if (g_strcmp0(key, "ElantpIapPassword") == 0) {
		if (!fu_strtoull(value, &tmp, 0, G_MAXUINT16, error))
			return FALSE;
		self->iap_password = (guint16)tmp;
		return TRUE;
	}
	if (g_strcmp0(key, "ElantpI2cTargetAddress") == 0) {
		if (!fu_strtoull(value, &tmp, 0, G_MAXUINT16, error))
			return FALSE;
		self->i2c_addr = (guint16)tmp;
		return TRUE;
	}
	g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_NOT_SUPPORTED, "quirk key not supported");
	return FALSE;
}

static void
fu_elantp_i2c_device_set_progress(FuDevice *self, FuProgress *progress)
{
	fu_progress_set_id(progress, G_STRLOC);
	fu_progress_add_flag(progress, FU_PROGRESS_FLAG_GUESSED);
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_RESTART, 2, "detach");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_WRITE, 94, "write");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_RESTART, 2, "attach");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_BUSY, 2, "reload");
}

static void
fu_elantp_i2c_device_init(FuElantpI2cDevice *self)
{
	fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_INTERNAL);
	fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_UPDATABLE);
	fu_device_set_summary(FU_DEVICE(self), "Touchpad (I²C)");
	fu_device_add_icon(FU_DEVICE(self), "input-touchpad");
	fu_device_add_protocol(FU_DEVICE(self), "tw.com.emc.elantp");
	fu_device_set_version_format(FU_DEVICE(self), FWUPD_VERSION_FORMAT_HEX);
	fu_udev_device_set_flags(FU_UDEV_DEVICE(self),
				 FU_UDEV_DEVICE_FLAG_OPEN_READ | FU_UDEV_DEVICE_FLAG_OPEN_WRITE);
	fu_device_register_private_flag(FU_DEVICE(self),
					FU_ELANTP_I2C_DEVICE_ABSOLUTE,
					"elantp-absolute");
}

static void
fu_elantp_i2c_device_finalize(GObject *object)
{
	FuElantpI2cDevice *self = FU_ELANTP_I2C_DEVICE(object);
	g_free(self->bind_path);
	g_free(self->bind_id);
	G_OBJECT_CLASS(fu_elantp_i2c_device_parent_class)->finalize(object);
}

static void
fu_elantp_i2c_device_class_init(FuElantpI2cDeviceClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS(klass);
	FuDeviceClass *klass_device = FU_DEVICE_CLASS(klass);
	object_class->finalize = fu_elantp_i2c_device_finalize;
	klass_device->to_string = fu_elantp_i2c_device_to_string;
	klass_device->attach = fu_elantp_i2c_device_attach;
	klass_device->set_quirk_kv = fu_elantp_i2c_device_set_quirk_kv;
	klass_device->setup = fu_elantp_i2c_device_setup;
	klass_device->reload = fu_elantp_i2c_device_setup;
	klass_device->write_firmware = fu_elantp_i2c_device_write_firmware;
	klass_device->prepare_firmware = fu_elantp_i2c_device_prepare_firmware;
	klass_device->probe = fu_elantp_i2c_device_probe;
	klass_device->open = fu_elantp_i2c_device_open;
	klass_device->set_progress = fu_elantp_i2c_device_set_progress;
}
