/*
 * Copyright 2020 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include "fu-elantp-common.h"
#include "fu-elantp-firmware.h"
#include "fu-elantp-hid-device.h"
#include "fu-elantp-hid-haptic-device.h"

struct _FuElantpHidDevice {
	FuHidrawDevice parent_instance;
	guint16 ic_page_count;
	guint16 ic_type;
	guint16 iap_type;
	guint16 iap_ctrl;
	guint16 iap_password;
	guint16 iap_ver;
	guint16 module_id;
	guint16 fw_page_size;
	gboolean force_table_support;
	guint32 force_table_addr;
	guint8 pattern;
};

G_DEFINE_TYPE(FuElantpHidDevice, fu_elantp_hid_device, FU_TYPE_HIDRAW_DEVICE)

static gboolean
fu_elantp_hid_device_detach(FuDevice *device, FuProgress *progress, GError **error);

static void
fu_elantp_hid_device_to_string(FuDevice *device, guint idt, GString *str)
{
	FuElantpHidDevice *self = FU_ELANTP_HID_DEVICE(device);
	fwupd_codec_string_append_hex(str, idt, "ModuleId", self->module_id);
	fwupd_codec_string_append_hex(str, idt, "Pattern", self->pattern);
	fwupd_codec_string_append_hex(str, idt, "FwPageSize", self->fw_page_size);
	fwupd_codec_string_append_hex(str, idt, "IcPageCount", self->ic_page_count);
	fwupd_codec_string_append_hex(str, idt, "IapType", self->iap_type);
	fwupd_codec_string_append_hex(str, idt, "IapCtrl", self->iap_ctrl);
}

static gboolean
fu_elantp_hid_device_probe(FuDevice *device, GError **error)
{
	guint16 device_id = fu_device_get_pid(device);

	/* check is valid */
	if (g_strcmp0(fu_udev_device_get_subsystem(FU_UDEV_DEVICE(device)), "hidraw") != 0) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_NOT_SUPPORTED,
			    "is not correct subsystem=%s, expected hidraw",
			    fu_udev_device_get_subsystem(FU_UDEV_DEVICE(device)));
		return FALSE;
	}

	/* i2c-hid */
	if (device_id != 0x400 && (device_id < 0x3000 || device_id >= 0x4000)) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_NOT_SUPPORTED,
				    "not i2c-hid touchpad");
		return FALSE;
	}

	/* success */
	return TRUE;
}

static gboolean
fu_elantp_hid_device_send_cmd(FuElantpHidDevice *self,
			      guint8 *tx,
			      gsize txsz,
			      guint8 *rx,
			      gsize rxsz,
			      GError **error)
{
	g_autofree guint8 *buf = NULL;
	gsize bufsz = rxsz + 3;

	if (!fu_hidraw_device_set_feature(FU_HIDRAW_DEVICE(self),
					  tx,
					  txsz,
					  FU_UDEV_DEVICE_IOCTL_FLAG_NONE,
					  error))
		return FALSE;
	if (rxsz == 0)
		return TRUE;

	/* GetFeature */
	buf = g_malloc0(bufsz);
	buf[0] = tx[0]; /* report number */
	if (!fu_hidraw_device_get_feature(FU_HIDRAW_DEVICE(self),
					  buf,
					  bufsz,
					  FU_UDEV_DEVICE_IOCTL_FLAG_NONE,
					  error))
		return FALSE;

	/* success */
	return fu_memcpy_safe(rx,
			      rxsz,
			      0x0, /* dst */
			      buf,
			      bufsz,
			      0x3, /* src */
			      rxsz,
			      error);
}

static gboolean
fu_elantp_hid_device_read_cmd(FuElantpHidDevice *self,
			      guint16 reg,
			      guint8 *rx,
			      gsize rxsz,
			      GError **error)
{
	guint8 buf[5] = {0x0d, 0x05, 0x03};
	fu_memwrite_uint16(buf + 0x3, reg, G_LITTLE_ENDIAN);
	return fu_elantp_hid_device_send_cmd(self, buf, sizeof(buf), rx, rxsz, error);
}

static gint
fu_elantp_hid_device_write_cmd(FuElantpHidDevice *self, guint16 reg, guint16 cmd, GError **error)
{
	guint8 buf[5] = {0x0d};
	fu_memwrite_uint16(buf + 0x1, reg, G_LITTLE_ENDIAN);
	fu_memwrite_uint16(buf + 0x3, cmd, G_LITTLE_ENDIAN);
	return fu_elantp_hid_device_send_cmd(self, buf, sizeof(buf), NULL, 0, error);
}

static gboolean
fu_elantp_hid_device_ensure_iap_ctrl(FuElantpHidDevice *self, GError **error)
{
	guint8 buf[2] = {0x0};
	if (!fu_elantp_hid_device_read_cmd(self, ETP_CMD_I2C_IAP_CTRL, buf, sizeof(buf), error)) {
		g_prefix_error(error, "failed to read IAPControl: ");
		return FALSE;
	}
	self->iap_ctrl = fu_memread_uint16(buf, G_LITTLE_ENDIAN);

	/* in bootloader mode? */
	if (self->force_table_support && self->iap_ver <= 5) {
		if ((self->iap_ctrl & ETP_I2C_MAIN_MODE_ON2) == 0)
			fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_IS_BOOTLOADER);
		else
			fu_device_remove_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_IS_BOOTLOADER);
		return TRUE;
	}

	if ((self->iap_ctrl & ETP_I2C_MAIN_MODE_ON) == 0)
		fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_IS_BOOTLOADER);
	else
		fu_device_remove_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_IS_BOOTLOADER);

	return TRUE;
}

static gboolean
fu_elantp_hid_device_read_force_table_enable(FuElantpHidDevice *self, GError **error)
{
	guint8 buf[2] = {0x0};
	guint16 value;

	if (!fu_elantp_hid_device_read_cmd(self,
					   ETP_CMD_I2C_FORCE_TYPE_ENABLE,
					   buf,
					   sizeof(buf),
					   error)) {
		g_prefix_error(error, "failed to read force type cmd: ");
		return FALSE;
	}
	value = fu_memread_uint16(buf, G_LITTLE_ENDIAN);
	if (value == 0xFFFF || value == ETP_CMD_I2C_FORCE_TYPE_ENABLE) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_NOT_SUPPORTED,
			    "forcetype cmd not supported");
		return FALSE;
	}
	if ((buf[0] & ETP_FW_FORCE_TYPE_ENABLE_BIT) == 0) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_NOT_SUPPORTED,
			    "force type table not supported");
		return FALSE;
	}

	/* success */
	return TRUE;
}

static gboolean
fu_elantp_hid_device_read_haptic_enable(FuElantpHidDevice *self, GError **error)
{
	guint8 buf[2] = {0x0};
	guint16 value;
	if (!fu_elantp_hid_device_read_cmd(self,
					   ETP_CMD_I2C_FORCE_TYPE_ENABLE,
					   buf,
					   sizeof(buf),
					   error)) {
		g_prefix_error(error, "failed to read haptic enable cmd: ");
		return FALSE;
	}
	value = fu_memread_uint16(buf, G_LITTLE_ENDIAN);
	if (value == 0xFFFF || value == ETP_CMD_I2C_FORCE_TYPE_ENABLE) {
		g_set_error(error, FWUPD_ERROR, FWUPD_ERROR_NOT_SUPPORTED, "not hapticpad");
		return FALSE;
	}

	if ((buf[0] & ETP_FW_FORCE_TYPE_ENABLE_BIT) == 0 ||
	    (buf[0] & ETP_FW_EEPROM_ENABLE_BIT) == 0) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_NOT_SUPPORTED,
			    "the haptic eeprom not supported");
		return FALSE;
	}

	/* success */
	return TRUE;
}

static gboolean
fu_elantp_hid_device_get_forcetable_address(FuElantpHidDevice *self, GError **error)
{
	guint8 buf[2] = {0x0};
	guint16 addr_wrds;

	if (self->iap_ver == 0x3) {
		self->force_table_addr = 0xFF40 * 2;
		return TRUE;
	}
	if (!fu_elantp_hid_device_read_cmd(self, ETP_CMD_FORCE_ADDR, buf, sizeof(buf), error)) {
		g_prefix_error(error, "failed to read force table address cmd: ");
		return FALSE;
	}
	addr_wrds = fu_memread_uint16(buf, G_LITTLE_ENDIAN);
	if (addr_wrds % 32 != 0) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_NOT_SUPPORTED,
			    "illegal force table address (%x)",
			    addr_wrds);
		return FALSE;
	}

	self->force_table_addr = addr_wrds * 2;

	/* success */
	return TRUE;
}

static gboolean
fu_elantp_hid_device_write_fw_password(FuElantpHidDevice *self,
				       guint16 ic_type,
				       guint16 iap_ver,
				       GError **error)
{
	guint8 buf[2] = {0x0};
	guint16 pw = ETP_I2C_IC13_IAPV5_PW;
	guint16 value;

	if (iap_ver < 0x5 || ic_type != 0x13)
		return TRUE;

	if (!fu_elantp_hid_device_write_cmd(self, ETP_CMD_I2C_FW_PW, pw, error)) {
		g_prefix_error(error, "failed to write fw password cmd: ");
		return FALSE;
	}

	if (!fu_elantp_hid_device_read_cmd(self, ETP_CMD_I2C_FW_PW, buf, sizeof(buf), error)) {
		g_prefix_error(error, "failed to read fw password cmd: ");
		return FALSE;
	}
	value = fu_memread_uint16(buf, G_LITTLE_ENDIAN);
	if (value != pw) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_NOT_SUPPORTED,
			    "can't set fw password got:%x",
			    value);
		return FALSE;
	}

	/* success */
	return TRUE;
}

static gboolean
fu_elantp_hid_device_setup(FuDevice *device, GError **error)
{
	FuElantpHidDevice *self = FU_ELANTP_HID_DEVICE(device);
	guint16 fwver;
	guint16 tmp;
	guint8 buf[2] = {0x0};
	g_autofree gchar *version_bl = NULL;
	g_autoptr(GError) error_local = NULL;
	g_autoptr(GError) error_forcetable = NULL;

	/* get pattern */
	if (!fu_elantp_hid_device_read_cmd(self, ETP_CMD_I2C_GET_HID_ID, buf, sizeof(buf), error)) {
		g_prefix_error(error, "failed to read HID ID: ");
		return FALSE;
	}
	tmp = fu_memread_uint16(buf, G_LITTLE_ENDIAN);
	self->pattern = tmp != 0xFFFF ? (tmp & 0xFF00) >> 8 : 0;

	/* get current firmware version */
	if (!fu_elantp_hid_device_read_cmd(self, ETP_CMD_I2C_FW_VERSION, buf, sizeof(buf), error)) {
		g_prefix_error(error, "failed to read fw version: ");
		return FALSE;
	}
	fwver = fu_memread_uint16(buf, G_LITTLE_ENDIAN);
	if (fwver == 0xFFFF || fwver == ETP_CMD_I2C_FW_VERSION)
		fwver = 0;
	fu_device_set_version_raw(device, fwver);

	/* get IAP firmware version */
	if (!fu_elantp_hid_device_read_cmd(self,
					   self->pattern == 0 ? ETP_CMD_I2C_IAP_VERSION
							      : ETP_CMD_I2C_IAP_VERSION_2,
					   buf,
					   sizeof(buf),
					   error)) {
		g_prefix_error(error, "failed to read bootloader version: ");
		return FALSE;
	}
	if (self->pattern >= 1) {
		self->iap_ver = buf[1];
	} else {
		self->iap_ver = fu_memread_uint16(buf, G_LITTLE_ENDIAN);
	}
	version_bl = fu_version_from_uint16(self->iap_ver, FWUPD_VERSION_FORMAT_HEX);
	fu_device_set_version_bootloader(device, version_bl);

	/* get module ID */
	if (!fu_elantp_hid_device_read_cmd(self, ETP_CMD_GET_MODULE_ID, buf, sizeof(buf), error)) {
		g_prefix_error(error, "failed to read module ID: ");
		return FALSE;
	}
	self->module_id = fu_memread_uint16(buf, G_LITTLE_ENDIAN);

	/* define the extra instance IDs */
	fu_device_add_instance_u16(device, "VEN", fu_device_get_vid(device));
	fu_device_add_instance_u16(device, "DEV", fu_device_get_pid(device));
	fu_device_add_instance_u16(device, "MOD", self->module_id);
	if (!fu_device_build_instance_id(device, error, "HIDRAW", "VEN", "DEV", "MOD", NULL))
		return FALSE;

	/* get OSM version */
	if (!fu_elantp_hid_device_read_cmd(self,
					   ETP_CMD_I2C_OSM_VERSION,
					   buf,
					   sizeof(buf),
					   error)) {
		g_prefix_error(error, "failed to read OSM version: ");
		return FALSE;
	}
	tmp = fu_memread_uint16(buf, G_LITTLE_ENDIAN);
	if (tmp == ETP_CMD_I2C_OSM_VERSION || tmp == 0xFFFF) {
		if (!fu_elantp_hid_device_read_cmd(self,
						   ETP_CMD_I2C_IAP_ICBODY,
						   buf,
						   sizeof(buf),
						   error)) {
			g_prefix_error(error, "failed to read IC body: ");
			return FALSE;
		}
		self->ic_type = fu_memread_uint16(buf, G_LITTLE_ENDIAN) & 0xFF;
	} else {
		self->ic_type = (tmp >> 8) & 0xFF;
	}

	/* define the extra instance IDs (ic_type + module_id + driver) */
	fu_device_add_instance_u8(device, "ICTYPE", self->ic_type);
	fu_device_build_instance_id_full(device,
					 FU_DEVICE_INSTANCE_FLAG_QUIRKS,
					 NULL,
					 "ELANTP",
					 "ICTYPE",
					 NULL);
	fu_device_build_instance_id(device, NULL, "ELANTP", "ICTYPE", "MOD", NULL);
	fu_device_add_instance_str(device, "DRIVER", "HID");
	fu_device_build_instance_id(device, NULL, "ELANTP", "ICTYPE", "MOD", "DRIVER", NULL);

	/* no quirk entry */
	if (self->ic_page_count == 0x0) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_NOT_SUPPORTED,
			    "no page count for ELANTP\\ICTYPE_%02X",
			    self->ic_type);
		return FALSE;
	}

	/* The ic_page_count is based on 64 bytes/page. */
	fu_device_set_firmware_size(device, (guint64)self->ic_page_count * (guint64)64);

	/* is in bootloader mode */
	if (!fu_elantp_hid_device_ensure_iap_ctrl(self, error))
		return FALSE;

	if (self->ic_type != 0x12 && self->ic_type != 0x13)
		return TRUE;

	if (!fu_elantp_hid_device_read_force_table_enable(self, &error_forcetable)) {
		g_debug("no forcetable detected: %s", error_forcetable->message);
	} else {
		if (!fu_elantp_hid_device_get_forcetable_address(self, error)) {
			g_prefix_error(error, "get forcetable address fail: ");
			return FALSE;
		}
		self->force_table_support = TRUE;
		/* is in bootloader mode */
		if (!fu_elantp_hid_device_ensure_iap_ctrl(self, error))
			return FALSE;
	}

	if (!fu_elantp_hid_device_read_haptic_enable(self, &error_local)) {
		g_debug("no haptic device detected: %s", error_local->message);
	} else {
		g_autoptr(FuElantpHidHapticDevice) cfg = fu_elantp_hid_haptic_device_new(device);
		fu_device_add_child(FU_DEVICE(device), FU_DEVICE(cfg));
	}

	/* fix an unsuitable i²c name, e.g. `VEN 04F3:00 04F3:3XXX` */
	if (g_str_has_prefix(fu_device_get_name(device), "VEN 04F3:00 04F3:3"))
		fu_device_set_name(device, "Touchpad");

	/* success */
	return TRUE;
}

static FuFirmware *
fu_elantp_hid_device_prepare_firmware(FuDevice *device,
				      GInputStream *stream,
				      FuProgress *progress,
				      FwupdInstallFlags flags,
				      GError **error)
{
	FuElantpHidDevice *self = FU_ELANTP_HID_DEVICE(device);
	guint16 module_id;
	guint16 ic_type;
	gboolean force_table_support;
	g_autoptr(FuFirmware) firmware = fu_elantp_firmware_new();

	/* check is compatible with hardware */
	if (!fu_firmware_parse_stream(firmware, stream, 0x0, flags, error))
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
	ic_type = fu_elantp_firmware_get_ic_type(FU_ELANTP_FIRMWARE(firmware));
	if (self->ic_type != ic_type) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INVALID_FILE,
			    "firmware ic type incompatible, got 0x%04x, expected 0x%04x",
			    ic_type,
			    self->ic_type);
		return NULL;
	}
	force_table_support =
	    fu_elantp_firmware_get_forcetable_support(FU_ELANTP_FIRMWARE(firmware));
	if (self->force_table_support != force_table_support) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INVALID_FILE,
			    "firmware incompatible, forcetable incorrect.");
		return NULL;
	}
	if (self->force_table_support) {
		guint32 force_table_addr;
		guint32 diff_size;
		force_table_addr =
		    fu_elantp_firmware_get_forcetable_addr(FU_ELANTP_FIRMWARE(firmware));
		if (self->force_table_addr < force_table_addr) {
			g_set_error(
			    error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INVALID_FILE,
			    "firmware forcetable address incompatible, got 0x%04x, expected 0x%04x",
			    force_table_addr / 2,
			    self->force_table_addr / 2);
			return NULL;
		}
		diff_size = self->force_table_addr - force_table_addr;
		if (diff_size % 64 != 0) {
			g_set_error(
			    error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INVALID_FILE,
			    "firmware forcetable address incompatible, got 0x%04x, expected 0x%04x",
			    force_table_addr / 2,
			    self->force_table_addr / 2);
			return NULL;
		}
	}

	/* success */
	return g_steal_pointer(&firmware);
}

static gboolean
fu_elantp_hid_device_filling_forcetable_firmware(FuDevice *device,
						 guint8 *fw_data,
						 gsize fw_size,
						 guint32 force_table_addr,
						 GError **error)
{
	FuElantpHidDevice *self = FU_ELANTP_HID_DEVICE(device);
	const guint8 fillature[] = {0x77, 0x33, 0x44, 0xaa};
	const guint8 signature[] = {0xAA, 0x55, 0xCC, 0x33, 0xFF, 0xFF};
	guint8 buf[64] = {[0 ... 63] = 0xFF};
	guint16 block_checksum;
	guint16 filling_value;

	if (self->force_table_addr == force_table_addr)
		return TRUE;

	if (self->force_table_addr < force_table_addr) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INVALID_FILE,
			    "forcetable address wrong (%x,%x): ",
			    force_table_addr,
			    self->force_table_addr);
		return FALSE;
	}

	if (!fu_memcpy_safe(buf,
			    sizeof(buf),
			    0, /* dst */
			    fillature,
			    sizeof(fillature),
			    0x0, /* src */
			    sizeof(fillature),
			    error))
		return FALSE;

	fu_memwrite_uint16(buf + 0x4, self->force_table_addr / 2, G_LITTLE_ENDIAN);
	if (!fu_memcpy_safe(buf,
			    sizeof(buf),
			    sizeof(buf) - 6, /* dst */
			    signature,
			    sizeof(signature),
			    0x0, /* src */
			    sizeof(signature),
			    error))
		return FALSE;

	block_checksum = fu_sum16w(buf, sizeof(buf), G_LITTLE_ENDIAN) - 0xFFFF;
	filling_value = 0x10000 - (block_checksum & 0xFFFF);
	fu_memwrite_uint16(buf + 0x6, filling_value, G_LITTLE_ENDIAN);

	for (guint i = force_table_addr; i < self->force_table_addr; i += 64) {
		if (!fu_memcpy_safe(fw_data,
				    fw_size,
				    i, /* dst */
				    buf,
				    sizeof(buf),
				    0x0, /* src */
				    sizeof(buf),
				    error))
			return FALSE;
	}
	return TRUE;
}

static gboolean
fu_elantp_hid_device_write_firmware(FuDevice *device,
				    FuFirmware *firmware,
				    FuProgress *progress,
				    FwupdInstallFlags flags,
				    GError **error)
{
	FuElantpHidDevice *self = FU_ELANTP_HID_DEVICE(device);
	FuElantpFirmware *firmware_elantp = FU_ELANTP_FIRMWARE(firmware);
	gsize bufsz = 0;
	guint16 checksum = 0;
	guint16 checksum_device = 0;
	guint16 iap_addr;
	const guint8 *buf;
	guint8 csum_buf[2] = {0x0};
	g_autoptr(GBytes) fw = NULL;
	g_autoptr(GPtrArray) chunks = NULL;
	guint total_pages;

	/* progress */
	fu_progress_set_id(progress, G_STRLOC);
	fu_progress_add_flag(progress, FU_PROGRESS_FLAG_GUESSED);
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_BUSY, 10, "detach");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_WRITE, 50, NULL);
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_VERIFY, 30, NULL);
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_RESTART, 10, "reset");

	/* simple image */
	fw = fu_firmware_get_bytes(firmware, error);
	if (fw == NULL)
		return FALSE;

	/* detach */
	if (!fu_elantp_hid_device_detach(device, fu_progress_get_child(progress), error))
		return FALSE;
	fu_progress_step_done(progress);

	/* write each block */
	buf = g_bytes_get_data(fw, &bufsz);
	iap_addr = fu_elantp_firmware_get_iap_addr(firmware_elantp);

	if (self->force_table_support &&
	    self->force_table_addr >=
		fu_elantp_firmware_get_forcetable_addr(FU_ELANTP_FIRMWARE(firmware))) {
		g_autofree guint8 *buf2 = g_malloc0(bufsz);
		if (!fu_memcpy_safe(buf2,
				    bufsz,
				    0x0, /* dst */
				    buf,
				    bufsz,
				    0x0, /* src */
				    bufsz,
				    error))
			return FALSE;

		if (!fu_elantp_hid_device_filling_forcetable_firmware(
			device,
			buf2,
			bufsz,
			fu_elantp_firmware_get_forcetable_addr(FU_ELANTP_FIRMWARE(firmware)),
			error)) {
			g_set_error(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_WRITE,
				    "filling forcetable failed");
			return FALSE;
		}
		chunks = fu_chunk_array_new(buf2 + iap_addr,
					    bufsz - iap_addr,
					    0x0,
					    0x0,
					    self->fw_page_size);
		total_pages = (self->force_table_addr - iap_addr - 1) / self->fw_page_size + 1;
		if (total_pages > chunks->len) {
			g_set_error(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_WRITE,
				    "total pages wrong (%u)",
				    total_pages);
			return FALSE;
		}
	} else {
		chunks = fu_chunk_array_new(buf + iap_addr,
					    bufsz - iap_addr,
					    0x0,
					    0x0,
					    self->fw_page_size);
		total_pages = chunks->len;
	}
	for (guint i = 0; i < total_pages; i++) {
		FuChunk *chk = g_ptr_array_index(chunks, i);
		guint16 csum_tmp =
		    fu_sum16w(fu_chunk_get_data(chk), fu_chunk_get_data_sz(chk), G_LITTLE_ENDIAN);
		gsize blksz = self->fw_page_size + 3;
		g_autofree guint8 *blk = g_malloc0(blksz);

		/* write block */
		blk[0] = 0x0B; /* report ID */
		if (!fu_memcpy_safe(blk,
				    blksz,
				    0x1, /* dst */
				    fu_chunk_get_data(chk),
				    fu_chunk_get_data_sz(chk),
				    0x0, /* src */
				    fu_chunk_get_data_sz(chk),
				    error))
			return FALSE;
		fu_memwrite_uint16(blk + fu_chunk_get_data_sz(chk) + 1, csum_tmp, G_LITTLE_ENDIAN);
		if (!fu_elantp_hid_device_send_cmd(self, blk, blksz, NULL, 0, error))
			return FALSE;
		fu_device_sleep(device,
				self->fw_page_size == 512 ? ELANTP_DELAY_WRITE_BLOCK_512
							  : ELANTP_DELAY_WRITE_BLOCK);

		if (!fu_elantp_hid_device_ensure_iap_ctrl(self, error))
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
	if (!fu_elantp_hid_device_read_cmd(self,
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
	fu_device_sleep_full(device,
			     ELANTP_DELAY_COMPLETE,
			     fu_progress_get_child(progress)); /* ms */
	fu_progress_step_done(progress);
	return TRUE;
}

static gboolean
fu_elantp_hid_device_detach(FuDevice *device, FuProgress *progress, GError **error)
{
	FuElantpHidDevice *self = FU_ELANTP_HID_DEVICE(device);
	guint16 iap_ver;
	guint16 ic_type;
	guint8 buf[2] = {0x0};
	guint16 tmp;

	/* sanity check */
	if (fu_device_has_flag(device, FWUPD_DEVICE_FLAG_IS_BOOTLOADER)) {
		g_info("in bootloader mode, reset IC");
		if (!fu_elantp_hid_device_write_cmd(self,
						    ETP_CMD_I2C_IAP_RESET,
						    ETP_I2C_IAP_RESET,
						    error))
			return FALSE;
		fu_device_sleep(FU_DEVICE(self), ELANTP_DELAY_RESET);
	}

	/* get OSM version */
	if (!fu_elantp_hid_device_read_cmd(self,
					   ETP_CMD_I2C_OSM_VERSION,
					   buf,
					   sizeof(buf),
					   error)) {
		g_prefix_error(error, "failed to read OSM version: ");
		return FALSE;
	}
	tmp = fu_memread_uint16(buf, G_LITTLE_ENDIAN);
	if (tmp == ETP_CMD_I2C_OSM_VERSION || tmp == 0xFFFF) {
		if (!fu_elantp_hid_device_read_cmd(self,
						   ETP_CMD_I2C_IAP_ICBODY,
						   buf,
						   sizeof(buf),
						   error)) {
			g_prefix_error(error, "failed to read IC body: ");
			return FALSE;
		}
		ic_type = fu_memread_uint16(buf, G_LITTLE_ENDIAN) & 0xFF;
	} else {
		ic_type = (tmp >> 8) & 0xFF;
	}

	/* get IAP firmware version */
	if (!fu_elantp_hid_device_read_cmd(self,
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
		iap_ver = fu_memread_uint16(buf, G_LITTLE_ENDIAN);
	}

	/* set the page size */
	self->fw_page_size = 64;
	if (ic_type >= 0x10) {
		if (iap_ver >= 1) {
			/* set the IAP type, presumably some kind of ABI */
			if (iap_ver >= 2 && (ic_type == 0x14 || ic_type == 0x15)) {
				self->fw_page_size = 512;
			} else {
				self->fw_page_size = 128;
			}

			if (!fu_elantp_hid_device_write_cmd(self,
							    ETP_CMD_I2C_IAP_TYPE,
							    self->fw_page_size / 2,
							    error))
				return FALSE;
			if (!fu_elantp_hid_device_read_cmd(self,
							   ETP_CMD_I2C_IAP_TYPE,
							   buf,
							   sizeof(buf),
							   error)) {
				g_prefix_error(error, "failed to read IAP type: ");
				return FALSE;
			}
			self->iap_type = fu_memread_uint16(buf, G_LITTLE_ENDIAN);
			if (self->iap_type != self->fw_page_size / 2) {
				g_set_error_literal(error,
						    FWUPD_ERROR,
						    FWUPD_ERROR_NOT_SUPPORTED,
						    "failed to set IAP type");
				return FALSE;
			}
		}
	}
	if (!fu_elantp_hid_device_write_fw_password(self, ic_type, iap_ver, error))
		return FALSE;
	if (!fu_elantp_hid_device_write_cmd(self, ETP_CMD_I2C_IAP, self->iap_password, error))
		return FALSE;
	fu_device_sleep(FU_DEVICE(self), ELANTP_DELAY_UNLOCK);
	if (!fu_elantp_hid_device_ensure_iap_ctrl(self, error))
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
fu_elantp_hid_device_attach(FuDevice *device, FuProgress *progress, GError **error)
{
	FuElantpHidDevice *self = FU_ELANTP_HID_DEVICE(device);

	/* sanity check */
	if (!fu_device_has_flag(device, FWUPD_DEVICE_FLAG_IS_BOOTLOADER)) {
		g_debug("already in runtime mode, skipping");
		return TRUE;
	}

	/* reset back to runtime */
	if (!fu_elantp_hid_device_write_cmd(self, ETP_CMD_I2C_IAP_RESET, ETP_I2C_IAP_RESET, error))
		return FALSE;
	fu_device_sleep(FU_DEVICE(self), ELANTP_DELAY_RESET);
	if (!fu_elantp_hid_device_write_cmd(self,
					    ETP_CMD_I2C_IAP_RESET,
					    ETP_I2C_ENABLE_REPORT,
					    error)) {
		g_prefix_error(error, "cannot enable TP report: ");
		return FALSE;
	}
	if (!fu_elantp_hid_device_write_cmd(self, 0x0306, 0x003, error)) {
		g_prefix_error(error, "cannot switch to TP PTP mode: ");
		return FALSE;
	}
	if (!fu_elantp_hid_device_ensure_iap_ctrl(self, error))
		return FALSE;

	/* success */
	return TRUE;
}

static gboolean
fu_elantp_hid_device_set_quirk_kv(FuDevice *device,
				  const gchar *key,
				  const gchar *value,
				  GError **error)
{
	FuElantpHidDevice *self = FU_ELANTP_HID_DEVICE(device);
	guint64 tmp = 0;

	if (g_strcmp0(key, "ElantpIcPageCount") == 0) {
		if (!fu_strtoull(value, &tmp, 0, G_MAXUINT16, FU_INTEGER_BASE_AUTO, error))
			return FALSE;
		self->ic_page_count = (guint16)tmp;
		return TRUE;
	}
	if (g_strcmp0(key, "ElantpIapPassword") == 0) {
		if (!fu_strtoull(value, &tmp, 0, G_MAXUINT16, FU_INTEGER_BASE_AUTO, error))
			return FALSE;
		self->iap_password = (guint16)tmp;
		return TRUE;
	}
	g_set_error_literal(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_NOT_SUPPORTED,
			    "quirk key not supported");
	return FALSE;
}

static void
fu_elantp_hid_device_set_progress(FuDevice *self, FuProgress *progress)
{
	fu_progress_set_id(progress, G_STRLOC);
	fu_progress_add_flag(progress, FU_PROGRESS_FLAG_GUESSED);
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_RESTART, 2, "detach");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_WRITE, 94, "write");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_RESTART, 2, "attach");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_BUSY, 2, "reload");
}

static gchar *
fu_elantp_hid_device_convert_version(FuDevice *device, guint64 version_raw)
{
	return fu_version_from_uint16(version_raw, fu_device_get_version_format(device));
}

static void
fu_elantp_hid_device_init(FuElantpHidDevice *self)
{
	fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_INTERNAL);
	fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_UPDATABLE);
	fu_device_set_summary(FU_DEVICE(self), "Touchpad");
	fu_device_add_icon(FU_DEVICE(self), "input-touchpad");
	fu_device_add_protocol(FU_DEVICE(self), "tw.com.emc.elantp");
	fu_device_set_vendor(FU_DEVICE(self), "ELAN Microelectronics");
	fu_device_set_version_format(FU_DEVICE(self), FWUPD_VERSION_FORMAT_HEX);
	fu_device_set_priority(FU_DEVICE(self), 1); /* better than i2c */
	fu_udev_device_add_open_flag(FU_UDEV_DEVICE(self), FU_IO_CHANNEL_OPEN_FLAG_READ);
	fu_udev_device_add_open_flag(FU_UDEV_DEVICE(self), FU_IO_CHANNEL_OPEN_FLAG_WRITE);
	fu_udev_device_add_open_flag(FU_UDEV_DEVICE(self), FU_IO_CHANNEL_OPEN_FLAG_NONBLOCK);
}

static void
fu_elantp_hid_device_finalize(GObject *object)
{
	G_OBJECT_CLASS(fu_elantp_hid_device_parent_class)->finalize(object);
}

static void
fu_elantp_hid_device_class_init(FuElantpHidDeviceClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS(klass);
	FuDeviceClass *device_class = FU_DEVICE_CLASS(klass);
	object_class->finalize = fu_elantp_hid_device_finalize;
	device_class->to_string = fu_elantp_hid_device_to_string;
	device_class->attach = fu_elantp_hid_device_attach;
	device_class->set_quirk_kv = fu_elantp_hid_device_set_quirk_kv;
	device_class->setup = fu_elantp_hid_device_setup;
	device_class->reload = fu_elantp_hid_device_setup;
	device_class->write_firmware = fu_elantp_hid_device_write_firmware;
	device_class->prepare_firmware = fu_elantp_hid_device_prepare_firmware;
	device_class->probe = fu_elantp_hid_device_probe;
	device_class->set_progress = fu_elantp_hid_device_set_progress;
	device_class->convert_version = fu_elantp_hid_device_convert_version;
}
