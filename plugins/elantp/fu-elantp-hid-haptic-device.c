/*
 * Copyright 2022 Jingle Wu <jingle.wu@emc.com.tw>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include "fu-elantp-common.h"
#include "fu-elantp-haptic-firmware.h"
#include "fu-elantp-hid-haptic-device.h"

struct _FuElantpHidHapticDevice {
	FuUdevDevice parent_instance;
	guint16 ic_page_count;
	guint16 iap_type;
	guint16 tp_iap_ctrl;
	guint16 tp_iap_ver;
	guint16 tp_ic_type;
	guint16 iap_ctrl;
	guint16 iap_password;
	guint16 module_id;
	guint16 fw_page_size;
	guint8 pattern;
	gint16 driver_ic;
	guint8 iap_ver;
};

G_DEFINE_TYPE(FuElantpHidHapticDevice, fu_elantp_hid_haptic_device, FU_TYPE_UDEV_DEVICE)

static FuElantpHidDevice *
fu_elantp_hid_haptic_device_get_parent(FuDevice *self, GError **error)
{
	FuDevice *parent = fu_device_get_parent(FU_DEVICE(self));
	if (parent == NULL) {
		g_set_error_literal(error, FWUPD_ERROR, FWUPD_ERROR_INTERNAL, "no parent set");
		return NULL;
	}
	return FU_ELANTP_HID_DEVICE(FU_UDEV_DEVICE(parent));
}

static gboolean
fu_elantp_hid_haptic_device_detach(FuDevice *device, FuProgress *progress, GError **error);

static void
fu_elantp_hid_haptic_device_to_string(FuDevice *device, guint idt, GString *str)
{
	FuElantpHidHapticDevice *self = FU_ELANTP_HID_HAPTIC_DEVICE(device);
	fwupd_codec_string_append_hex(str, idt, "ModuleId", self->module_id);
	fwupd_codec_string_append_hex(str, idt, "Pattern", self->pattern);
	fwupd_codec_string_append_hex(str, idt, "FwPageSize", self->fw_page_size);
	fwupd_codec_string_append_hex(str, idt, "IcPageCount", self->ic_page_count);
	fwupd_codec_string_append_hex(str, idt, "IapType", self->iap_type);
	fwupd_codec_string_append_hex(str, idt, "TpIapCtrl", self->tp_iap_ctrl);
	fwupd_codec_string_append_hex(str, idt, "IapCtrl", self->iap_ctrl);
	fwupd_codec_string_append_hex(str, idt, "DriverIC", self->driver_ic);
	fwupd_codec_string_append_hex(str, idt, "IAPVersion", self->iap_ver);
}

static gboolean
fu_elantp_hid_haptic_device_send_cmd(FuDevice *self,
				     const guint8 *tx,
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
fu_elantp_hid_haptic_device_read_cmd(FuDevice *self,
				     guint16 reg,
				     guint8 *buf,
				     gsize bufz,
				     GError **error)
{
	guint8 tmp[5] = {0x0D, 0x05, 0x03};
	fu_memwrite_uint16(tmp + 0x3, reg, G_LITTLE_ENDIAN);
	return fu_elantp_hid_haptic_device_send_cmd(self, tmp, sizeof(tmp), buf, bufz, error);
}

static gint
fu_elantp_hid_haptic_device_write_cmd(FuDevice *self, guint16 reg, guint16 cmd, GError **error)
{
	guint8 buf[5] = {0x0D};
	fu_memwrite_uint16(buf + 0x1, reg, G_LITTLE_ENDIAN);
	fu_memwrite_uint16(buf + 0x3, cmd, G_LITTLE_ENDIAN);
	return fu_elantp_hid_haptic_device_send_cmd(self, buf, sizeof(buf), NULL, 0, error);
}

static gboolean
fu_elantp_hid_haptic_device_ensure_iap_ctrl(FuDevice *parent,
					    FuElantpHidHapticDevice *self,
					    GError **error)
{
	guint8 buf[2] = {0x0};
	if (!fu_elantp_hid_haptic_device_read_cmd(parent,
						  ETP_CMD_I2C_IAP_CTRL,
						  buf,
						  sizeof(buf),
						  error)) {
		g_prefix_error(error, "failed to read IAPControl: ");
		return FALSE;
	}
	self->tp_iap_ctrl = fu_memread_uint16(buf, G_LITTLE_ENDIAN);

	/* in bootloader mode? */
	if (self->tp_iap_ver <= 5) {
		if ((self->tp_iap_ctrl & ETP_I2C_MAIN_MODE_ON2) == 0)
			fu_device_add_flag(FU_DEVICE(parent), FWUPD_DEVICE_FLAG_IS_BOOTLOADER);
		else
			fu_device_remove_flag(FU_DEVICE(parent), FWUPD_DEVICE_FLAG_IS_BOOTLOADER);
		return TRUE;
	}

	if ((self->tp_iap_ctrl & ETP_I2C_MAIN_MODE_ON) == 0)
		fu_device_add_flag(FU_DEVICE(parent), FWUPD_DEVICE_FLAG_IS_BOOTLOADER);
	else
		fu_device_remove_flag(FU_DEVICE(parent), FWUPD_DEVICE_FLAG_IS_BOOTLOADER);

	return TRUE;
}

static gboolean
fu_elantp_hid_haptic_device_ensure_eeprom_iap_ctrl(FuDevice *parent,
						   FuElantpHidHapticDevice *self,
						   GError **error)
{
	guint8 buf[2] = {0x0};
	if (!fu_elantp_hid_haptic_device_read_cmd(parent,
						  ETP_CMD_I2C_SET_EEPROM_CTRL,
						  buf,
						  sizeof(buf),
						  error)) {
		g_prefix_error(error, "failed to read IAPControl: ");
		return FALSE;
	}
	self->iap_ctrl = fu_memread_uint16(buf, G_LITTLE_ENDIAN);

	if ((self->iap_ctrl & 0x800) != 0x800) {
		g_set_error(error, FWUPD_ERROR, FWUPD_ERROR_INVALID_DATA, "bit11 fail");
		return FALSE;
	}
	if ((self->iap_ctrl & 0x1000) == 0x1000) {
		g_set_error(error, FWUPD_ERROR, FWUPD_ERROR_BUSY, "bit12 fail, resend");
		return FALSE;
	}

	return TRUE;
}

static gboolean
fu_elantp_hid_haptic_device_get_haptic_driver_ic(FuDevice *parent,
						 FuElantpHidHapticDevice *self,
						 GError **error)
{
	guint8 buf[2] = {0x0};
	guint16 value;
	if (!fu_elantp_hid_haptic_device_read_cmd(parent,
						  ETP_CMD_I2C_FORCE_TYPE_ENABLE,
						  buf,
						  sizeof(buf),
						  error)) {
		g_prefix_error(error, "failed to read haptic enable cmd: ");
		return FALSE;
	}
	value = fu_memread_uint16(buf, G_LITTLE_ENDIAN);
	if (value == 0xFFFF || value == ETP_CMD_I2C_FORCE_TYPE_ENABLE) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_NOT_SUPPORTED,
				    "failed to read haptic enable cmd");
		return FALSE;
	}

	if ((buf[0] & ETP_FW_FORCE_TYPE_ENABLE_BIT) == 0 ||
	    (buf[0] & ETP_FW_EEPROM_ENABLE_BIT) == 0) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_NOT_SUPPORTED,
				    "eeprom enable bit unset");
		return FALSE;
	}

	/* success */
	self->driver_ic = (buf[0] >> 4) & 0xF;
	return TRUE;
}

static gboolean
fu_elantp_hid_haptic_device_get_version(FuDevice *parent,
					FuElantpHidHapticDevice *self,
					GError **error)
{
	guint16 v_s = 0;
	guint16 v_d = 0;
	guint16 v_m = 0;
	guint16 v_y = 0;
	guint8 buf[2] = {0x0};

	if (!fu_elantp_hid_haptic_device_write_cmd(parent,
						   ETP_CMD_I2C_SET_EEPROM_CTRL,
						   ETP_CMD_I2C_GET_EEPROM_FW_VERSION,
						   error)) {
		g_prefix_error(error, "failed to write haptic version cmd: ");
		return FALSE;
	}
	fu_device_sleep(FU_DEVICE(self), ELANTP_DELAY_RESET);

	if (!fu_elantp_hid_haptic_device_read_cmd(parent, 0x0321, buf, sizeof(buf), error)) {
		g_prefix_error(error, "failed to read haptic version cmd: ");
		return FALSE;
	}
	v_d = buf[0];
	v_m = buf[1] & 0xF;
	v_s = (buf[1] & 0xF0) >> 4;

	if (!fu_elantp_hid_haptic_device_write_cmd(parent,
						   ETP_CMD_I2C_SET_EEPROM_CTRL,
						   ETP_CMD_I2C_GET_EEPROM_IAP_VERSION,
						   error)) {
		g_prefix_error(error, "failed to write haptic iap version cmd: ");
		return FALSE;
	}
	fu_device_sleep(FU_DEVICE(self), ELANTP_DELAY_RESET);

	if (!fu_elantp_hid_haptic_device_read_cmd(parent, 0x0321, buf, sizeof(buf), error)) {
		g_prefix_error(error, "failed to read haptic iap version cmd: ");
		return FALSE;
	}
	v_y = buf[0];
	self->iap_ver = buf[1];

	if (v_y == 0xFF && v_d == 0xFF && v_m == 0xF) {
		fu_device_set_version(FU_DEVICE(self), "0");
	} else {
		g_autofree gchar *str = g_strdup_printf("%02d%02d%02d%02d", v_y, v_m, v_d, v_s);
		fu_device_set_version(FU_DEVICE(self), str);
	}

	return TRUE;
}

static gboolean
fu_elantp_hid_haptic_device_write_fw_password(FuDevice *parent,
					      guint16 tp_ic_type,
					      guint16 tp_iap_ver,
					      GError **error)
{
	guint8 buf[2] = {0x0};
	guint16 pw = ETP_I2C_IC13_IAPV5_PW;
	guint16 value;

	if (tp_iap_ver < 0x5 || tp_ic_type != 0x13)
		return TRUE;

	if (!fu_elantp_hid_haptic_device_write_cmd(parent, ETP_CMD_I2C_FW_PW, pw, error)) {
		g_prefix_error(error, "failed to write fw password cmd: ");
		return FALSE;
	}

	if (!fu_elantp_hid_haptic_device_read_cmd(parent,
						  ETP_CMD_I2C_FW_PW,
						  buf,
						  sizeof(buf),
						  error)) {
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

typedef struct {
	guint16 checksum;
	guint16 iap_password;
	guint16 tp_iap_ver;
	guint16 tp_ic_type;
} FuElantpHaptictpWaitFlashEEPROMChecksumHelper;

static gboolean
fu_elantp_hid_haptic_device_write_checksum_cb(FuDevice *parent, gpointer user_data, GError **error)
{
	guint8 buf[2] = {0x0};
	guint16 value;
	FuElantpHaptictpWaitFlashEEPROMChecksumHelper *helper = user_data;

	if (!fu_elantp_hid_haptic_device_write_cmd(parent,
						   ETP_CMD_I2C_EEPROM_SETTING,
						   ETP_CMD_I2C_EEPROM_WRITE_INFORMATION,
						   error)) {
		g_prefix_error(error, "failed to write haptic info: ");
		return FALSE;
	}
	if (!fu_elantp_hid_haptic_device_read_cmd(parent,
						  ETP_CMD_I2C_EEPROM_SETTING,
						  buf,
						  sizeof(buf),
						  error)) {
		g_prefix_error(error, "failed to read haptic info: ");
		return FALSE;
	}
	value = fu_memread_uint16(buf, G_LITTLE_ENDIAN);

	if ((value & 0xFFFF) != ETP_CMD_I2C_EEPROM_WRITE_INFORMATION) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_WRITE,
			    "failed to set haptic info (0x%04x): ",
			    value);
		return FALSE;
	}
	if (!fu_elantp_hid_haptic_device_write_fw_password(parent,
							   helper->tp_ic_type,
							   helper->tp_iap_ver,
							   error))
		return FALSE;
	if (!fu_elantp_hid_haptic_device_write_cmd(parent,
						   ETP_CMD_I2C_IAP,
						   helper->iap_password,
						   error)) {
		g_prefix_error(error, "failed to write iap password: ");
		return FALSE;
	}
	if (!fu_elantp_hid_haptic_device_write_cmd(parent,
						   ETP_CMD_I2C_EEPROM_WRITE_CHECKSUM,
						   helper->checksum,
						   error)) {
		g_prefix_error(error, "failed to write eeprom checksum: ");
		return FALSE;
	}
	if (!fu_elantp_hid_haptic_device_write_cmd(parent,
						   ETP_CMD_I2C_EEPROM_SETTING,
						   ETP_CMD_I2C_EEPROM_SETTING_INITIAL,
						   error)) {
		g_prefix_error(error, "failed to set haptic initial setting: ");
		return FALSE;
	}
	if (!fu_elantp_hid_haptic_device_read_cmd(parent,
						  ETP_CMD_I2C_EEPROM_WRITE_CHECKSUM,
						  buf,
						  sizeof(buf),
						  error)) {
		g_prefix_error(error, "failed to read haptic checksum: ");
		return FALSE;
	}
	value = fu_memread_uint16(buf, G_LITTLE_ENDIAN);
	if ((value & 0xFFFF) != helper->checksum) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INVALID_DATA,
			    "eeprom checksum failed 0x%04x != 0x%04x : ",
			    value,
			    helper->checksum);
		return FALSE;
	}

	/* success */
	return TRUE;
}

static gboolean
fu_elantp_hid_haptic_device_wait_calc_checksum_cb(FuDevice *parent,
						  gpointer user_data,
						  GError **error)
{
	guint16 ctrl;
	guint8 buf[2] = {0x0};

	if (!fu_elantp_hid_haptic_device_write_cmd(parent,
						   ETP_CMD_I2C_SET_EEPROM_CTRL,
						   ETP_CMD_I2C_SET_EEPROM_DATATYPE,
						   error)) {
		g_prefix_error(error, "failed to write eeprom datatype: ");
		return FALSE;
	}
	if (!fu_elantp_hid_haptic_device_read_cmd(parent,
						  ETP_CMD_I2C_SET_EEPROM_CTRL,
						  buf,
						  sizeof(buf),
						  error)) {
		g_prefix_error(error, "failed to read calc haptic cmd: ");
		return FALSE;
	}
	ctrl = fu_memread_uint16(buf, G_LITTLE_ENDIAN);
	if ((ctrl & 0x20) == 0x20) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INVALID_DATA,
			    "ctrl failed 0x%04x",
			    ctrl);
		return FALSE;
	}

	return TRUE;
}

static gboolean
fu_elantp_hid_haptic_device_get_checksum(FuDevice *parent, guint16 *checksum, GError **error)
{
	guint8 buf[2] = {0x0};
	g_autoptr(GError) error_local = NULL;

	if (!fu_elantp_hid_haptic_device_write_cmd(parent,
						   ETP_CMD_I2C_SET_EEPROM_CTRL,
						   ETP_CMD_I2C_CALC_EEPROM_CHECKSUM,
						   error))
		return FALSE;
	if (!fu_device_retry_full(parent,
				  fu_elantp_hid_haptic_device_wait_calc_checksum_cb,
				  100,
				  ELANTP_EEPROM_READ_DELAY,
				  NULL,
				  &error_local)) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_READ,
			    "failed to wait calc eeprom checksum (%s)",
			    error_local->message);
		return FALSE;
	}
	if (!fu_elantp_hid_haptic_device_write_cmd(parent,
						   ETP_CMD_I2C_SET_EEPROM_CTRL,
						   ETP_CMD_I2C_READ_EEPROM_CHECKSUM,
						   error))
		return FALSE;
	if (!fu_elantp_hid_haptic_device_read_cmd(parent,
						  ETP_CMD_I2C_SET_EEPROM_CTRL,
						  buf,
						  sizeof(buf),
						  error)) {
		g_prefix_error(error, "failed to read haptic checksum cmd: ");
		return FALSE;
	}
	*checksum = fu_memread_uint16(buf, G_LITTLE_ENDIAN);

	return TRUE;
}

static gboolean
fu_elantp_hid_haptic_device_setup(FuDevice *device, GError **error)
{
	FuElantpHidDevice *parent;
	FuElantpHidHapticDevice *self = FU_ELANTP_HID_HAPTIC_DEVICE(device);
	FuUdevDevice *udev_parent;
	guint8 ic_type;
	guint16 tmp;
	guint8 buf[2] = {0x0};
	g_autofree gchar *version_bl = NULL;

	parent = fu_elantp_hid_haptic_device_get_parent(device, error);
	if (parent == NULL)
		return FALSE;

	if (!fu_elantp_hid_haptic_device_get_haptic_driver_ic(FU_DEVICE(parent), self, error)) {
		g_prefix_error(error, "this module is not support haptic EEPROM: ");
		return FALSE;
	}

	/* get pattern */
	if (!fu_elantp_hid_haptic_device_read_cmd(FU_DEVICE(parent),
						  ETP_CMD_I2C_GET_HID_ID,
						  buf,
						  sizeof(buf),
						  error)) {
		g_prefix_error(error, "failed to read HID ID: ");
		return FALSE;
	}
	tmp = fu_memread_uint16(buf, G_LITTLE_ENDIAN);
	self->pattern = tmp != 0xFFFF ? (tmp & 0xFF00) >> 8 : 0;

	if (!fu_elantp_hid_haptic_device_get_version(FU_DEVICE(parent), self, error))
		return FALSE;

	version_bl = fu_version_from_uint16(self->iap_ver, FWUPD_VERSION_FORMAT_HEX);
	fu_device_set_version_bootloader(device, version_bl);

	/* get module ID */
	if (!fu_elantp_hid_haptic_device_read_cmd(FU_DEVICE(parent),
						  ETP_CMD_GET_MODULE_ID,
						  buf,
						  sizeof(buf),
						  error)) {
		g_prefix_error(error, "failed to read module ID: ");
		return FALSE;
	}
	self->module_id = fu_memread_uint16(buf, G_LITTLE_ENDIAN);

	/* define the extra instance IDs */
	udev_parent = FU_UDEV_DEVICE(parent);
	fu_device_add_instance_u16(device, "VEN", fu_device_get_vid(FU_DEVICE(udev_parent)));
	fu_device_add_instance_u16(device, "DEV", fu_device_get_pid(FU_DEVICE(udev_parent)));
	fu_device_add_instance_u16(device, "DRIVERIC", self->driver_ic);
	fu_device_add_instance_u16(device, "MOD", self->module_id);
	if (!fu_device_build_instance_id(device,
					 error,
					 "HIDRAW",
					 "VEN",
					 "DEV",
					 "DRIVERIC",
					 "MOD",
					 NULL))
		return FALSE;

	/* get OSM version */
	if (!fu_elantp_hid_haptic_device_read_cmd(FU_DEVICE(parent),
						  ETP_CMD_I2C_OSM_VERSION,
						  buf,
						  sizeof(buf),
						  error)) {
		g_prefix_error(error, "failed to read OSM version: ");
		return FALSE;
	}
	tmp = fu_memread_uint16(buf, G_LITTLE_ENDIAN);
	if (tmp == ETP_CMD_I2C_OSM_VERSION || tmp == 0xFFFF) {
		if (!fu_elantp_hid_haptic_device_read_cmd(FU_DEVICE(parent),
							  ETP_CMD_I2C_IAP_ICBODY,
							  buf,
							  sizeof(buf),
							  error)) {
			g_prefix_error(error, "failed to read IC body: ");
			return FALSE;
		}
		ic_type = fu_memread_uint16(buf, G_LITTLE_ENDIAN) & 0xFF;
	} else
		ic_type = (tmp >> 8) & 0xFF;

	/* define the extra instance IDs (ic_type + module_id + driver) */
	fu_device_add_instance_u8(device, "ICTYPE", ic_type);
	fu_device_build_instance_id_full(device,
					 FU_DEVICE_INSTANCE_FLAG_QUIRKS,
					 NULL,
					 "ELANTP",
					 "ICTYPE",
					 NULL);
	fu_device_build_instance_id(device, NULL, "ELANTP", "ICTYPE", "DRIVERIC", "MOD", NULL);
	fu_device_add_instance_str(device, "DRIVER", "HID");
	fu_device_build_instance_id(device,
				    NULL,
				    "ELANTP",
				    "ICTYPE",
				    "DRIVERIC",
				    "MOD",
				    "DRIVER",
				    NULL);

	/* no quirk entry */
	if (self->ic_page_count == 0x0) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_NOT_SUPPORTED,
			    "no page count for ELANTP\\ICTYPE_%02X",
			    ic_type);
		return FALSE;
	}

	fu_device_set_firmware_size(device, 32768);

	/* find out if in bootloader mode */
	if (!fu_elantp_hid_haptic_device_ensure_iap_ctrl(FU_DEVICE(parent), self, error))
		return FALSE;

	/* success */
	return TRUE;
}

static FuFirmware *
fu_elantp_hid_haptic_device_prepare_firmware(FuDevice *device,
					     GInputStream *stream,
					     FuProgress *progress,
					     FwupdInstallFlags flags,
					     GError **error)
{
	FuElantpHidHapticDevice *self = FU_ELANTP_HID_HAPTIC_DEVICE(device);
	guint16 driver_ic;
	g_autoptr(FuFirmware) firmware = fu_elantp_haptic_firmware_new();

	/* check is compatible with hardware */
	if (!fu_firmware_parse_stream(firmware, stream, 0x0, flags, error))
		return NULL;
	driver_ic = fu_elantp_haptic_firmware_get_driver_ic(FU_ELANTP_HAPTIC_FIRMWARE(firmware));
	if (driver_ic != self->driver_ic) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_NOT_SUPPORTED,
			    "driver IC 0x%x != 0x%x",
			    (guint)driver_ic,
			    (guint)self->driver_ic);
		return NULL;
	}

	/* success */
	return g_steal_pointer(&firmware);
}

typedef struct {
	guint16 checksum;
	guint idx_page_start;
	GBytes *fw;	      /* noref */
	FuProgress *progress; /* noref */
} FuElantpHaptictpWriteHelper;

static gboolean
fu_elantp_hid_haptic_device_write_chunks_cb(FuDevice *device, gpointer user_data, GError **error)
{
	FuElantpHaptictpWriteHelper *helper = (FuElantpHaptictpWriteHelper *)user_data;
	FuElantpHidHapticDevice *self = FU_ELANTP_HID_HAPTIC_DEVICE(device);
	FuElantpHidDevice *parent;
	const guint16 eeprom_fw_page_size = 32;
	g_autoptr(FuChunkArray) chunks = NULL;

	/* use parent */
	parent = fu_elantp_hid_haptic_device_get_parent(device, error);
	if (parent == NULL)
		return FALSE;

	/* progress */
	chunks = fu_chunk_array_new_from_bytes(helper->fw, 0x0, eeprom_fw_page_size);
	fu_progress_set_id(helper->progress, G_STRLOC);
	fu_progress_set_steps(helper->progress,
			      fu_chunk_array_length(chunks) - helper->idx_page_start + 1);
	for (guint i = helper->idx_page_start; i <= fu_chunk_array_length(chunks); i++) {
		guint16 csum_tmp;
		gsize blksz = self->fw_page_size + 3;
		g_autofree guint8 *blk = g_malloc0(blksz);
		g_autoptr(FuChunk) chk = NULL;
		g_autoptr(GError) error_iapctrl = NULL;

		if (i == fu_chunk_array_length(chunks))
			chk = fu_chunk_array_index(chunks, 0, error);
		else
			chk = fu_chunk_array_index(chunks, i, error);
		if (chk == NULL)
			return FALSE;

		/* write block */
		blk[0] = 0x0B; /* report ID */
		blk[1] = eeprom_fw_page_size + 5;
		blk[2] = 0xA2;
		fu_memwrite_uint16(blk + 0x3, i * eeprom_fw_page_size, G_BIG_ENDIAN);

		if (i == 0) {
			guint8 first_page[32] = {0x0};
			memset(&first_page[0], 0xFF, sizeof(first_page));
			csum_tmp = fu_sum16(first_page, eeprom_fw_page_size);
			if (!fu_memcpy_safe(blk,
					    blksz,
					    0x5, /* dst */
					    first_page,
					    eeprom_fw_page_size,
					    0x0, /* src */
					    eeprom_fw_page_size,
					    error))
				return FALSE;

			fu_memwrite_uint16(blk + eeprom_fw_page_size + 5, csum_tmp, G_BIG_ENDIAN);
			csum_tmp = 0;
		} else {
			csum_tmp = fu_sum16(fu_chunk_get_data(chk), fu_chunk_get_data_sz(chk));
			if (!fu_memcpy_safe(blk,
					    blksz,
					    0x5, /* dst */
					    fu_chunk_get_data(chk),
					    fu_chunk_get_data_sz(chk),
					    0x0, /* src */
					    fu_chunk_get_data_sz(chk),
					    error))
				return FALSE;

			fu_memwrite_uint16(blk + fu_chunk_get_data_sz(chk) + 5,
					   csum_tmp,
					   G_BIG_ENDIAN);
		}

		if (!fu_elantp_hid_haptic_device_send_cmd(FU_DEVICE(parent),
							  blk,
							  blksz,
							  NULL,
							  0,
							  error))
			return FALSE;

		fu_device_sleep(device,
				self->fw_page_size == 512 ? ELANTP_DELAY_WRITE_BLOCK_512
							  : ELANTP_DELAY_WRITE_BLOCK);

		if (!fu_elantp_hid_haptic_device_write_cmd(FU_DEVICE(parent),
							   ETP_CMD_I2C_SET_EEPROM_CTRL,
							   ETP_CMD_I2C_SET_EEPROM_DATATYPE,
							   error))
			return FALSE;

		if (!fu_elantp_hid_haptic_device_ensure_eeprom_iap_ctrl(FU_DEVICE(parent),
									self,
									&error_iapctrl)) {
			g_set_error(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_WRITE,
				    "bootloader reports failed write: 0x%x (%s)",
				    self->iap_ctrl,
				    error_iapctrl->message);
			return FALSE;
		}

		/* update progress */
		helper->checksum += csum_tmp;
		helper->idx_page_start = i + 1;
		fu_progress_step_done(helper->progress);
	}

	/* success */
	return TRUE;
}

static gboolean
fu_elantp_hid_haptic_device_write_firmware(FuDevice *device,
					   FuFirmware *firmware,
					   FuProgress *progress,
					   FwupdInstallFlags flags,
					   GError **error)
{
	FuElantpHidDevice *parent;
	FuElantpHidHapticDevice *self = FU_ELANTP_HID_HAPTIC_DEVICE(device);
	guint16 checksum_device = 0;
	const gchar *fw_ver;
	const gchar *fw_ver_device;
	g_autoptr(GBytes) fw = NULL;
	g_autoptr(GError) error_local = NULL;
	FuElantpHaptictpWaitFlashEEPROMChecksumHelper helper = {0x0};
	FuElantpHaptictpWriteHelper helper_write = {0x0};

	/* progress */
	fu_progress_set_id(progress, G_STRLOC);
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_BUSY, 5, "detach");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_WRITE, 80, NULL);
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_VERIFY, 10, NULL);
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_RESTART, 5, NULL);

	/* simple image */
	fw = fu_firmware_get_bytes(firmware, error);
	if (fw == NULL)
		return FALSE;

	/* use parent */
	parent = fu_elantp_hid_haptic_device_get_parent(device, error);
	if (parent == NULL)
		return FALSE;

	/* detach */
	if (!fu_elantp_hid_haptic_device_detach(device, fu_progress_get_child(progress), error))
		return FALSE;
	fu_progress_step_done(progress);

	/* write each block */
	helper_write.fw = fw;
	helper_write.progress = fu_progress_get_child(progress);
	if (!fu_device_retry_full(device,
				  fu_elantp_hid_haptic_device_write_chunks_cb,
				  3,
				  100,
				  &helper_write,
				  error))
		return FALSE;
	fu_progress_step_done(progress);

	if (!fu_elantp_hid_haptic_device_write_cmd(FU_DEVICE(parent),
						   ETP_CMD_I2C_EEPROM_SETTING,
						   ETP_CMD_I2C_EEPROM_SETTING_INITIAL,
						   error)) {
		g_prefix_error(error, "cannot disable EEPROM Long Transmission mode: ");
		return FALSE;
	}
	if (!fu_elantp_hid_haptic_device_write_cmd(FU_DEVICE(parent),
						   ETP_CMD_I2C_SET_EEPROM_CTRL,
						   ETP_CMD_I2C_SET_EEPROM_LEAVE_IAP,
						   error)) {
		g_prefix_error(error, "cannot leave EEPROM IAP: ");
		return FALSE;
	}
	fu_device_sleep(device, ELANTP_DELAY_RESET);
	if (!fu_elantp_hid_haptic_device_get_checksum(FU_DEVICE(parent), &checksum_device, error)) {
		g_prefix_error(error, "read device checksum fail: ");
		return FALSE;
	}
	if (helper_write.checksum != checksum_device) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_WRITE,
			    "checksum failed 0x%04x != 0x%04x",
			    helper_write.checksum,
			    checksum_device);
		return FALSE;
	}

	helper.checksum = checksum_device;
	helper.iap_password = self->iap_password;
	helper.tp_ic_type = self->tp_ic_type;
	helper.tp_iap_ver = self->tp_iap_ver;
	if (!fu_device_retry_full(FU_DEVICE(parent),
				  fu_elantp_hid_haptic_device_write_checksum_cb,
				  3,
				  ELANTP_DELAY_WRITE_BLOCK,
				  &helper,
				  &error_local)) {
		g_prefix_error(error, "write device checksum fail (%s): ", error_local->message);
		return FALSE;
	}

	if (!fu_elantp_hid_haptic_device_get_version(FU_DEVICE(parent), self, error))
		return FALSE;
	fw_ver_device = fu_device_get_version(device);
	fw_ver = fu_firmware_get_version(firmware);

	if (g_strcmp0(fw_ver_device, fw_ver) != 0) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_WRITE,
			    "firmware version failed %s != %s",
			    fw_ver,
			    fw_ver_device);
		return FALSE;
	}
	fu_progress_step_done(progress);

	if (!fu_elantp_hid_haptic_device_write_cmd(FU_DEVICE(parent),
						   ETP_CMD_I2C_SET_EEPROM_CTRL,
						   ETP_CMD_I2C_HAPTIC_RESTART,
						   error)) {
		g_prefix_error(error, "cannot restart haptic DriverIC: ");
		return FALSE;
	}
	fu_progress_step_done(progress);

	return TRUE;
}

static gboolean
fu_elantp_hid_haptic_device_detach(FuDevice *device, FuProgress *progress, GError **error)
{
	FuElantpHidDevice *parent;
	FuElantpHidHapticDevice *self = FU_ELANTP_HID_HAPTIC_DEVICE(device);
	guint8 buf[2] = {0x0};
	guint16 ctrl;
	guint16 tmp;

	/* haptic EEPROM IAP process runs in the TP main code */
	if (fu_device_has_flag(device, FWUPD_DEVICE_FLAG_IS_BOOTLOADER)) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_NOT_SUPPORTED,
			    "in touchpad bootloader mode");
		return FALSE;
	}

	if (self->driver_ic != 0x2 || self->iap_ver != 0x1) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_NOT_SUPPORTED,
			    "no support for EEPROM IAP 0x%x 0x%x: ",
			    (guint)self->driver_ic,
			    (guint)self->iap_ver);
		return FALSE;
	}
	parent = fu_elantp_hid_haptic_device_get_parent(device, error);
	if (parent == NULL)
		return FALSE;

	/* get OSM version */
	if (!fu_elantp_hid_haptic_device_read_cmd(FU_DEVICE(parent),
						  ETP_CMD_I2C_OSM_VERSION,
						  buf,
						  sizeof(buf),
						  error)) {
		g_prefix_error(error, "failed to read OSM version: ");
		return FALSE;
	}
	tmp = fu_memread_uint16(buf, G_LITTLE_ENDIAN);
	if (tmp == ETP_CMD_I2C_OSM_VERSION || tmp == 0xFFFF) {
		if (!fu_elantp_hid_haptic_device_read_cmd(FU_DEVICE(parent),
							  ETP_CMD_I2C_IAP_ICBODY,
							  buf,
							  sizeof(buf),
							  error)) {
			g_prefix_error(error, "failed to read IC body: ");
			return FALSE;
		}
		self->tp_ic_type = fu_memread_uint16(buf, G_LITTLE_ENDIAN) & 0xFF;
	} else
		self->tp_ic_type = (tmp >> 8) & 0xFF;

	/* get IAP firmware version */
	if (!fu_elantp_hid_haptic_device_read_cmd(FU_DEVICE(parent),
						  self->pattern == 0 ? ETP_CMD_I2C_IAP_VERSION
								     : ETP_CMD_I2C_IAP_VERSION_2,
						  buf,
						  sizeof(buf),
						  error)) {
		g_prefix_error(error, "failed to read bootloader version: ");
		return FALSE;
	}
	if (self->pattern >= 1)
		self->tp_iap_ver = buf[1];
	else
		self->tp_iap_ver = fu_memread_uint16(buf, G_LITTLE_ENDIAN);

	/* set the page size */
	self->fw_page_size = 64;
	if (self->tp_ic_type >= 0x10) {
		if (self->tp_iap_ver >= 1) {
			/* set the IAP type, presumably some kind of ABI */
			if (self->tp_iap_ver >= 2 &&
			    (self->tp_ic_type == 0x14 || self->tp_ic_type == 0x15)) {
				self->fw_page_size = 512;
			} else {
				self->fw_page_size = 128;
			}

			if (!fu_elantp_hid_haptic_device_write_cmd(FU_DEVICE(parent),
								   ETP_CMD_I2C_IAP_TYPE,
								   self->fw_page_size / 2,
								   error))
				return FALSE;
			if (!fu_elantp_hid_haptic_device_read_cmd(FU_DEVICE(parent),
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

	if (!fu_elantp_hid_haptic_device_write_cmd(FU_DEVICE(parent),
						   ETP_CMD_I2C_EEPROM_SETTING,
						   ETP_CMD_I2C_EEPROM_LONG_TRANS_ENABLE,
						   error)) {
		g_prefix_error(error, "cannot enable EEPROM Long Transmission mode: ");
		return FALSE;
	}

	if (!fu_elantp_hid_haptic_device_write_cmd(FU_DEVICE(parent),
						   ETP_CMD_I2C_SET_EEPROM_CTRL,
						   ETP_CMD_I2C_SET_EEPROM_ENTER_IAP,
						   error)) {
		g_prefix_error(error, "cannot enter EEPROM IAP: ");
		return FALSE;
	}

	if (!fu_elantp_hid_haptic_device_read_cmd(FU_DEVICE(parent),
						  ETP_CMD_I2C_SET_EEPROM_CTRL,
						  buf,
						  sizeof(buf),
						  error))
		return FALSE;
	ctrl = fu_memread_uint16(buf, G_LITTLE_ENDIAN);
	if ((ctrl & 0x800) == 0x800) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_READ,
			    "unexpected EEPROM bootloader control %x",
			    ctrl);
		return FALSE;
	}

	/* success */
	return TRUE;
}

static gboolean
fu_elantp_hid_haptic_device_attach(FuDevice *device, FuProgress *progress, GError **error)
{
	FuElantpHidDevice *parent;
	FuElantpHidHapticDevice *self = FU_ELANTP_HID_HAPTIC_DEVICE(device);

	parent = fu_elantp_hid_haptic_device_get_parent(device, error);
	if (parent == NULL)
		return FALSE;

	/* reset back to runtime */
	if (!fu_elantp_hid_haptic_device_write_cmd(FU_DEVICE(parent),
						   ETP_CMD_I2C_IAP_RESET,
						   ETP_I2C_IAP_RESET,
						   error)) {
		g_prefix_error(error, "cannot reset TP: ");
		return FALSE;
	}
	fu_device_sleep(device, ELANTP_DELAY_RESET);
	if (!fu_elantp_hid_haptic_device_write_cmd(FU_DEVICE(parent),
						   ETP_CMD_I2C_IAP_RESET,
						   ETP_I2C_ENABLE_REPORT,
						   error)) {
		g_prefix_error(error, "cannot enable TP report: ");
		return FALSE;
	}
	if (!fu_elantp_hid_haptic_device_write_cmd(FU_DEVICE(parent), 0x0306, 0x003, error)) {
		g_prefix_error(error, "cannot switch to TP PTP mode: ");
		return FALSE;
	}
	if (!fu_elantp_hid_haptic_device_ensure_iap_ctrl(FU_DEVICE(parent), self, error))
		return FALSE;

	/* sanity check */
	if (fu_device_has_flag(device, FWUPD_DEVICE_FLAG_IS_BOOTLOADER)) {
		g_set_error(error, FWUPD_ERROR, FWUPD_ERROR_READ, "in bootloader mode");
		return FALSE;
	}

	/* success */
	return TRUE;
}

static gboolean
fu_elantp_hid_haptic_device_set_quirk_kv(FuDevice *device,
					 const gchar *key,
					 const gchar *value,
					 GError **error)
{
	FuElantpHidHapticDevice *self = FU_ELANTP_HID_HAPTIC_DEVICE(device);
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
fu_elantp_hid_haptic_device_set_progress(FuDevice *self, FuProgress *progress)
{
	fu_progress_set_id(progress, G_STRLOC);
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_RESTART, 5, "detach");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_WRITE, 85, "write");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_RESTART, 2, "attach");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_BUSY, 8, "reload");
}

static void
fu_elantp_hid_haptic_device_init(FuElantpHidHapticDevice *self)
{
	fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_INTERNAL);
	fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_UPDATABLE);
	fu_device_add_private_flag(FU_DEVICE(self), FU_DEVICE_PRIVATE_FLAG_USE_PARENT_FOR_OPEN);
	fu_device_add_icon(FU_DEVICE(self), "input-touchpad");
	fu_device_add_protocol(FU_DEVICE(self), "tw.com.emc.elantp.haptic");
	fu_device_set_name(FU_DEVICE(self), "HapticPad EEPROM");
	fu_device_set_logical_id(FU_DEVICE(self), "eeprom");
	fu_device_set_version_format(FU_DEVICE(self), FWUPD_VERSION_FORMAT_PLAIN);
	fu_device_set_priority(FU_DEVICE(self), 1); /* better than i2c */
}

static void
fu_elantp_hid_haptic_device_class_init(FuElantpHidHapticDeviceClass *klass)
{
	FuDeviceClass *device_class = FU_DEVICE_CLASS(klass);
	device_class->to_string = fu_elantp_hid_haptic_device_to_string;
	device_class->attach = fu_elantp_hid_haptic_device_attach;
	device_class->set_quirk_kv = fu_elantp_hid_haptic_device_set_quirk_kv;
	device_class->setup = fu_elantp_hid_haptic_device_setup;
	device_class->reload = fu_elantp_hid_haptic_device_setup;
	device_class->write_firmware = fu_elantp_hid_haptic_device_write_firmware;
	device_class->prepare_firmware = fu_elantp_hid_haptic_device_prepare_firmware;
	device_class->set_progress = fu_elantp_hid_haptic_device_set_progress;
}

FuElantpHidHapticDevice *
fu_elantp_hid_haptic_device_new(FuDevice *device)
{
	FuElantpHidHapticDevice *self;
	self = g_object_new(FU_TYPE_ELANTP_HID_HAPTIC_DEVICE, NULL);
	return FU_ELANTP_HID_HAPTIC_DEVICE(self);
}
