/*
 * Copyright (C) 2020 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include <fwupdplugin.h>
#include <stdio.h>
#include <linux/hidraw.h>
#include <linux/input.h>

#include "fu-elantp-common.h"
#include "fu-elanhaptic-firmware.h"
#include "fu-elantp-hid-haptic-device.h"

struct _FuElantpHidHapticDevice {
	FuUdevDevice parent_instance;
	guint16 ic_page_count;
	guint16 iap_type;
	guint16 tp_iap_ctrl;
	guint16 iap_ctrl;
	guint16 iap_password;
	guint16 module_id;
	guint16 fw_page_size;
	guint8 pattern;
	gint16 driver_ic;
	guint8 iap_ver;
};

G_DEFINE_TYPE(FuElantpHidHapticDevice, fu_elantp_hid_haptic_device, FU_TYPE_UDEV_DEVICE)

#define FU_ELANTP_DEVICE_IOCTL_TIMEOUT 5000 /* ms */

static FuElantpHidDevice *
fu_elantp_haptic_device_get_parent(FuDevice *self, GError **error)
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
	fu_string_append_kx(str, idt, "ModuleId", self->module_id);
	fu_string_append_kx(str, idt, "Pattern", self->pattern);
	fu_string_append_kx(str, idt, "FwPageSize", self->fw_page_size);
	fu_string_append_kx(str, idt, "IcPageCount", self->ic_page_count);
	fu_string_append_kx(str, idt, "IapType", self->iap_type);
	fu_string_append_kx(str, idt, "TpIapCtrl", self->tp_iap_ctrl);
	fu_string_append_kx(str, idt, "IapCtrl", self->iap_ctrl);
	fu_string_append_kx(str, idt, "DriverIC", self->driver_ic);
	fu_string_append_kx(str, idt, "IAPVersion", self->iap_ver);
}

static gboolean
fu_elantp_hid_haptic_device_probe(FuDevice *device, GError **error)
{
	/* set the physical ID */
	return fu_udev_device_set_physical_id(FU_UDEV_DEVICE(device), "hid", error);
}

static gboolean
fu_elantp_hid_haptic_device_send_cmd(FuDevice *self,
			      guint8 *tx,
			      gsize txsz,
			      guint8 *rx,
			      gsize rxsz,
			      GError **error)
{

	g_autofree guint8 *buf = NULL;
	gsize bufsz = rxsz + 3;

	if (g_getenv("FWUPD_ELANTP_VERBOSE") != NULL)
		fu_dump_raw(G_LOG_DOMAIN, "SetReport", tx, txsz);

	if (!fu_udev_device_ioctl(FU_UDEV_DEVICE(self),
				  HIDIOCSFEATURE(txsz),
				  tx,
				  NULL,
				  FU_ELANTP_DEVICE_IOCTL_TIMEOUT,
				  error)) 
		return FALSE;

	if (rxsz == 0)
		return TRUE;

	/* GetFeature */
	buf = g_malloc0(bufsz);
	buf[0] = tx[0]; /* report number */
	if (!fu_udev_device_ioctl(FU_UDEV_DEVICE(self),
				  HIDIOCGFEATURE(bufsz),
				  buf,
				  NULL,
				  FU_ELANTP_DEVICE_IOCTL_TIMEOUT,
				  error))
		return FALSE;
	
	if (g_getenv("FWUPD_ELANTP_VERBOSE") != NULL)
		fu_dump_raw(G_LOG_DOMAIN, "GetReport", buf, bufsz);

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
			      guint8 *rx,
			      gsize rxsz,
			      GError **error)
{
	guint8 buf[5] = {0x0d, 0x05, 0x03};
	fu_memwrite_uint16(buf + 0x3, reg, G_LITTLE_ENDIAN);
	return fu_elantp_hid_haptic_device_send_cmd(self, buf, sizeof(buf), rx, rxsz, error);
}

static gint
fu_elantp_hid_haptic_device_write_cmd(FuDevice *self, guint16 reg, guint16 cmd, GError **error)
{
	guint8 buf[5] = {0x0d};
	fu_memwrite_uint16(buf + 0x1, reg, G_LITTLE_ENDIAN);
	fu_memwrite_uint16(buf + 0x3, cmd, G_LITTLE_ENDIAN);
	return fu_elantp_hid_haptic_device_send_cmd(self, buf, sizeof(buf), NULL, 0, error);
}

static gboolean
fu_elantp_hid_haptic_device_ensure_iap_ctrl(FuDevice *parent, FuElantpHidHapticDevice *self, GError **error)
{
	guint8 buf[2] = {0x0};
	if (!fu_elantp_hid_haptic_device_read_cmd(parent, ETP_CMD_I2C_IAP_CTRL, buf, sizeof(buf), error)) {
		g_prefix_error(error, "failed to read IAPControl: ");
		return FALSE;
	}
	self->tp_iap_ctrl = fu_memread_uint16(buf, G_LITTLE_ENDIAN);

	/* in bootloader mode? */
	if ((self->tp_iap_ctrl & ETP_I2C_MAIN_MODE_ON) == 0)
		fu_device_add_flag(FU_DEVICE(parent), FWUPD_DEVICE_FLAG_IS_BOOTLOADER);
	else
		fu_device_remove_flag(FU_DEVICE(parent), FWUPD_DEVICE_FLAG_IS_BOOTLOADER);

	return TRUE;
}

static gint16
fu_elantp_hid_haptic_device_ensure_eeprom_iap_ctrl(FuDevice *parent, FuElantpHidHapticDevice *self, GError **error)
{
	guint8 buf[2] = {0x0};
	if (!fu_elantp_hid_haptic_device_read_cmd(parent, 
						ETP_CMD_I2C_SET_EEPROM_CTRL, 
						buf, 
						sizeof(buf), 
						error)) {
		g_prefix_error(error, "failed to read IAPControl: ");
		return -1;
	}
	self->iap_ctrl = fu_memread_uint16(buf, G_LITTLE_ENDIAN);

	if ((self->iap_ctrl & 0x800) != 0x800) {
        	g_prefix_error(error, "bit11 fail: ");
		return -2;
    	}
    	if ((self->iap_ctrl & 0x1000) == 0x1000) {
       		g_prefix_error(error, "bit12 fail resend: ");
		return 0;
    	}
    
	return 1;
}

static gboolean
fu_elantp_hid_haptic_device_get_hatpic_driver_ic(FuDevice *parent, FuElantpHidHapticDevice *self, GError **error)
{
	guint8 buf[2] = {0x0};
	guint16 value;
	if (!fu_elantp_hid_haptic_device_read_cmd(parent, 
						ETP_CMD_I2C_FLIM_TYPE_ENABLE, 
						buf, 
						sizeof(buf), 
						error)) {
		g_prefix_error(error, "failed to read haptic enable cmd: ");
		return FALSE;
	}
	value = fu_memread_uint16(buf, G_LITTLE_ENDIAN);
	
	if (value == 0xFFFF || value == ETP_CMD_I2C_FLIM_TYPE_ENABLE) {
		g_prefix_error(error, "failed to read haptic enable cmd: ");
		return FALSE;
	}

	if (buf[0] & ETP_FW_FLIM_TYPE_ENABLE_BIT && buf[0] & ETP_FW_EEPROM_ENABLE_BIT) {
		self->driver_ic = (buf[0] >> 4) & 0xF;
		return TRUE;
	}
	
	return FALSE;
}

static guint32
fu_elantp_hid_haptic_device_get_version(FuDevice *parent, FuElantpHidHapticDevice *self, GError **error)
{
    	guint16 v_s = 0;
    	guint16 v_d = 0;
    	guint16 v_m = 0;
    	guint16 v_y = 0;
    	gint8 tmp[256] = {0x0};
	guint8 buf[2] = {0x0};

	if (!fu_elantp_hid_haptic_device_write_cmd(parent,
						ETP_CMD_I2C_SET_EEPROM_CTRL,
						ETP_CMD_I2C_GET_EEPROM_FW_VERSION,
						error))
		return 0;

    	g_usleep(ELANTP_DELAY_RESET * 1000); 

	if (!fu_elantp_hid_haptic_device_read_cmd(parent, 0x0321, buf, sizeof(buf), error)) {
		g_prefix_error(error, "failed to read haptic version cmd: ");
		return -1;
	}
    	v_d = buf[0];
    	v_m = buf[1] & 0xF;
   	v_s = (buf[1] & 0xF0) >> 4;

	if (!fu_elantp_hid_haptic_device_write_cmd(parent,
						ETP_CMD_I2C_SET_EEPROM_CTRL,
						ETP_CMD_I2C_GET_EEPROM_IAP_VERSION,
						error))
		return 0;

    	g_usleep(ELANTP_DELAY_RESET * 1000); 

	if (!fu_elantp_hid_haptic_device_read_cmd(parent, 0x0321, buf, sizeof(buf), error)) {
		g_prefix_error(error, "failed to read haptic version cmd: ");
		return 0;
	}
    	v_y = buf[0];
    	self->iap_ver = buf[1];

    	if (v_y==0xFF || v_m==0xFF || v_d==0xFF || v_s==0xFF)
		return 0;

    	sprintf((char*)tmp, "%02d%02d%02d%02d", v_y, v_m, v_d, v_s);
    	
    	return atoi((char*)tmp);
}

static guint16
fu_elantp_hid_haptic_device_get_checksum(FuDevice *parent, GError **error)
{
    	guint16 ctrl = 0;
	int cnt = 0;
	guint8 buf[2] = {0x0};
	
	if (!fu_elantp_hid_haptic_device_write_cmd(parent,
						ETP_CMD_I2C_SET_EEPROM_CTRL,
						ETP_CMD_I2C_CALC_EEPROM_CHECKSUM,
						error))
		return 0;

wait:
    	g_usleep(ELANTP_EEPROM_READ_DELAY * 1000);
	if (!fu_elantp_hid_haptic_device_write_cmd(parent,
						ETP_CMD_I2C_SET_EEPROM_CTRL,
						ETP_CMD_I2C_SET_EEPROM_DATATYPE,
						error))
		return 0;
		
	if (!fu_elantp_hid_haptic_device_read_cmd(parent, ETP_CMD_I2C_SET_EEPROM_CTRL, buf, sizeof(buf), error)) {
		g_prefix_error(error, "failed to read calc haptic cmd: ");
		return 0;
	}
	ctrl = fu_memread_uint16(buf, G_LITTLE_ENDIAN);
	
	if ((ctrl & 0x20) == 0x20) {
		cnt ++;
		if (cnt >= 100) {
			g_prefix_error(error, "failed to wait calc haptic cmd: ");
			return 0;	
		}
		goto wait;
	}
	if (!fu_elantp_hid_haptic_device_write_cmd(parent,
						ETP_CMD_I2C_SET_EEPROM_CTRL,
						ETP_CMD_I2C_READ_EEPROM_CHECKSUM,
						error))
		return 0;
	if (!fu_elantp_hid_haptic_device_read_cmd(parent, ETP_CMD_I2C_SET_EEPROM_CTRL, buf, sizeof(buf), error)) {
		g_prefix_error(error, "failed to read haptic checksum cmd: ");
		return 0;
	}
	
    	return fu_memread_uint16(buf, G_LITTLE_ENDIAN);
}

static gboolean
fu_elantp_hid_haptic_device_setup(FuDevice *device, GError **error)
{
	FuElantpHidDevice *parent = fu_elantp_haptic_device_get_parent(device, error);
	FuElantpHidHapticDevice *self = FU_ELANTP_HID_HAPTIC_DEVICE(device);
	FuUdevDevice *udev_device = FU_UDEV_DEVICE(parent);
	guint32 fwver;
	guint8 ic_type;
	guint16 tmp;
	guint8 buf[2] = {0x0};
	g_autofree gchar *version_bl = NULL;
	g_autofree gchar *version = NULL; 

	if (parent == NULL)
		return FALSE;
		
	if (!fu_elantp_hid_haptic_device_get_hatpic_driver_ic(FU_DEVICE(parent), self, error)) {
		g_prefix_error(error, "this module is not support haptic EEPROM.");
		return FALSE;
	}

	/* get pattern */
	if (!fu_elantp_hid_haptic_device_read_cmd(FU_DEVICE(parent), ETP_CMD_I2C_GET_HID_ID, buf, sizeof(buf), error)) {
		g_prefix_error(error, "failed to read HID ID: ");
		return FALSE;
	}
	tmp = fu_memread_uint16(buf, G_LITTLE_ENDIAN);
	self->pattern = tmp != 0xffff ? (tmp & 0xff00) >> 8 : 0;

	fwver = fu_elantp_hid_haptic_device_get_version(FU_DEVICE(parent), self, error);

	version = fu_version_from_uint32(fwver, FWUPD_VERSION_FORMAT_HEX);
	fu_device_set_version(device, version);

	version_bl = fu_version_from_uint16(self->iap_ver, FWUPD_VERSION_FORMAT_HEX);
	fu_device_set_version_bootloader(device, version_bl);

	/* get module ID */
	if (!fu_elantp_hid_haptic_device_read_cmd(FU_DEVICE(parent), ETP_CMD_GET_MODULE_ID, buf, sizeof(buf), error)) {
		g_prefix_error(error, "failed to read module ID: ");
		return FALSE;
	}
	self->module_id = fu_memread_uint16(buf, G_LITTLE_ENDIAN);

	/* define the extra instance IDs */
	fu_device_add_instance_u16(device, "VEN", fu_udev_device_get_vendor(udev_device));
	fu_device_add_instance_u16(device, "DEV", fu_udev_device_get_model(udev_device));
	fu_device_add_instance_u16(device, "DRIVERIC", self->driver_ic);
	fu_device_add_instance_u16(device, "MOD", self->module_id);
	if (!fu_device_build_instance_id(device, error, "HIDRAW", "VEN", "DEV", "DRIVERIC", "MOD", NULL))
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
	fu_device_build_instance_id(device, NULL, "ELANTP", "ICTYPE", NULL);
	fu_device_build_instance_id(device, NULL, "ELANTP", "ICTYPE", "", NULL);
	fu_device_build_instance_id(device, NULL, "ELANTP", "ICTYPE", "DRIVERIC", "MOD", NULL);
	fu_device_add_instance_str(device, "DRIVER", "HID");
	fu_device_build_instance_id(device, NULL, "ELANTP", "ICTYPE", "DRIVERIC", "MOD", "DRIVER", NULL);

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

	/* is in bootloader mode */
	if (!fu_elantp_hid_haptic_device_ensure_iap_ctrl(FU_DEVICE(parent), self, error))
		return FALSE;

	/* success */
	return TRUE;
}

static FuFirmware *
fu_elantp_hid_haptic_device_prepare_firmware(FuDevice *device,
				      GBytes *fw,
				      FwupdInstallFlags flags,
				      GError **error)
{
	g_autoptr(FuFirmware) firmware = fu_elanhaptic_firmware_new();

	/* check is compatible with hardware */
	if (!fu_firmware_parse(firmware, fw, flags, error))
		return NULL;
		
	/* success */
	return g_steal_pointer(&firmware);
}

static gboolean
fu_elantp_hid_haptic_device_write_firmware(FuDevice *device,
				    FuFirmware *firmware,
				    FuProgress *progress,
				    FwupdInstallFlags flags,
				    GError **error)
{
	FuElantpHidDevice *parent = fu_elantp_haptic_device_get_parent(device, error);
	FuElantpHidHapticDevice *self = FU_ELANTP_HID_HAPTIC_DEVICE(device);
	FuElanhapticFirmware *firmware_elanhaptic = FU_ELANHAPTIC_FIRMWARE(firmware);
	gsize bufsz = 0;
	guint16 checksum = 0;
	guint16 checksum_device = 0;
	gint16 ctrl_result = 0;
	guint16 retry_cnt = 0;
	guint16 eeprom_fw_page_size = 32;
	guint16 driver_ic = 0;
	guint32 fw_ver = 0;
	guint32 fw_ver_device = 0;
	const guint8 *buf;
	g_autoptr(GBytes) fw = NULL;
	g_autoptr(GPtrArray) chunks = NULL;
	guint8 first_page[32];

	/* progress */
	fu_progress_set_id(progress, G_STRLOC);
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_BUSY, 5, "detach");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_WRITE, 85, NULL);
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_VERIFY, 10, NULL);

	/* simple image */
	fw = fu_firmware_get_bytes(firmware, error);
	if (fw == NULL)
		return FALSE;

	driver_ic = fu_elanhaptic_firmware_get_driveric(firmware_elanhaptic);

	if (driver_ic != self->driver_ic)
		return FALSE;

	if (parent == NULL)
		return FALSE;
		
	/* detach */
	if (!fu_elantp_hid_haptic_device_detach(device, fu_progress_get_child(progress), error))
		return FALSE;
	fu_progress_step_done(progress);

	/* write each block */
	buf = g_bytes_get_data(fw, &bufsz);
	chunks = fu_chunk_array_new(buf, bufsz, 0x0, 0x0, eeprom_fw_page_size);

	for (guint i = 0; i <= chunks->len; i++) {
		guint16 index = i * eeprom_fw_page_size;
		FuChunk *chk ;
		guint16 csum_tmp;
		gsize blksz = self->fw_page_size + 3;
		g_autofree guint8 *blk = g_malloc0(blksz);

		if (i == chunks->len)
			chk = g_ptr_array_index(chunks, 0);
		else
			chk = g_ptr_array_index(chunks, i);

		/* write block */
		blk[0] = 0x0B; /* report ID */
		blk[1] = eeprom_fw_page_size + 5;
		blk[2] = 0xA2;
		blk[3] = index / 256;
		blk[4] = index % 256;

		if (i == 0) {			
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
				
			fu_memwrite_uint16(blk + fu_chunk_get_data_sz(chk) + 5, csum_tmp, G_BIG_ENDIAN);
		}

		if (!fu_elantp_hid_haptic_device_send_cmd(FU_DEVICE(parent), blk, blksz, NULL, 0, error))
			return FALSE;
			
		g_usleep(self->fw_page_size == 512 ? ELANTP_DELAY_WRITE_BLOCK_512 * 1000
						   : ELANTP_DELAY_WRITE_BLOCK * 1000);

		if (!fu_elantp_hid_haptic_device_write_cmd(FU_DEVICE(parent),
							ETP_CMD_I2C_SET_EEPROM_CTRL,
							ETP_CMD_I2C_SET_EEPROM_DATATYPE,
							error))
				return FALSE;

		ctrl_result = fu_elantp_hid_haptic_device_ensure_eeprom_iap_ctrl(FU_DEVICE(parent),self, error);

		if (ctrl_result == 1) {
			retry_cnt = 0;	

			/* update progress */
			checksum += csum_tmp;
			fu_progress_set_percentage_full(fu_progress_get_child(progress),
							(gsize)i + 1,
							(gsize)chunks->len + 1);
		} else if (ctrl_result == 0) {
			i -= 1;
			retry_cnt++;
			if (retry_cnt >= 3) {
				g_set_error(error,
					FWUPD_ERROR,
					FWUPD_ERROR_WRITE,
					"bootloader reports failed write: 0x%x",
					self->iap_ctrl);
				return FALSE;
			}
		} else {
			g_set_error(error,
			    	FWUPD_ERROR,
				FWUPD_ERROR_WRITE,
				"bootloader reports failed write: 0x%x",
				self->iap_ctrl);
			return FALSE;
		}
	}
	fu_progress_step_done(progress);	
	
	if (!fu_elantp_hid_haptic_device_write_cmd(FU_DEVICE(parent),
					    	ETP_CMD_I2C_EEPROM_LONG_TRANS,
					    	ETP_CMD_I2C_EEPROM_LONG_TRANS_DISABLE,
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
	checksum_device = fu_elantp_hid_haptic_device_get_checksum(FU_DEVICE(parent), error);

	if (checksum != checksum_device) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_WRITE,
			    "checksum failed 0x%04x != 0x%04x",
			    checksum,
			    checksum_device);
		return FALSE;
	}
	
	fw_ver = fu_elanhaptic_firmware_get_fwver(firmware_elanhaptic);
	fw_ver_device = fu_elantp_hid_haptic_device_get_version(FU_DEVICE(parent), self, error);
	if (fw_ver != fw_ver_device) {
		return FALSE;
	}

	fu_progress_step_done(progress);
	return TRUE;
}

static gboolean
fu_elantp_hid_haptic_device_detach(FuDevice *device, FuProgress *progress, GError **error)
{
	FuElantpHidDevice *parent = fu_elantp_haptic_device_get_parent(device, error);
	FuElantpHidHapticDevice *self = FU_ELANTP_HID_HAPTIC_DEVICE(device);
	guint16 tp_iap_ver;
	guint16 tp_ic_type;
	guint8 buf[2] = {0x0};
	guint16 ctrl; 
	guint16 tmp;
	
	/* sanity check */
	if (fu_device_has_flag(device, FWUPD_DEVICE_FLAG_IS_BOOTLOADER)) 
		return FALSE;

	if (self->driver_ic != 0x2 || self->iap_ver != 0x1) {
		g_prefix_error(error, "Can't support this EEPROM IAP: ");
        	return FALSE;
   	}

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
		tp_ic_type = fu_memread_uint16(buf, G_LITTLE_ENDIAN) & 0xFF;
	} else 
		tp_ic_type = (tmp >> 8) & 0xFF;

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
		tp_iap_ver = buf[1];
	else
		tp_iap_ver = fu_memread_uint16(buf, G_LITTLE_ENDIAN);

	/* set the page size */
	self->fw_page_size = 64;
	if (tp_ic_type >= 0x10) {
		if (tp_iap_ver >= 1) {
			/* set the IAP type, presumably some kind of ABI */
			if (tp_iap_ver >= 2 && (tp_ic_type == 0x14 || tp_ic_type == 0x15)) {
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
					    	ETP_CMD_I2C_EEPROM_LONG_TRANS,
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
	if ((ctrl & 0x800) != 0x800) {
        	/* success */
		g_debug("fu_elantp_hid_haptic_device_detach ctrl %x", ctrl);
		return TRUE;
	}

	g_prefix_error(error, "unexpected EEPROM bootloader control");
	return FALSE;
}

static gboolean
fu_elantp_hid_haptic_device_attach(FuDevice *device, FuProgress *progress, GError **error)
{
	FuElantpHidDevice *parent = fu_elantp_haptic_device_get_parent(device, error);
	FuElantpHidHapticDevice *self = FU_ELANTP_HID_HAPTIC_DEVICE(device);
	
	if (parent == NULL)
		return FALSE;
		
	/* reset back to runtime */
	if (!fu_elantp_hid_haptic_device_write_cmd(FU_DEVICE(parent), 
						ETP_CMD_I2C_IAP_RESET, 
						ETP_I2C_IAP_RESET, 
						error))
		return FALSE;
	g_usleep(ELANTP_DELAY_RESET * 1000);
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
	if (!fu_device_has_flag(device, FWUPD_DEVICE_FLAG_IS_BOOTLOADER)) 
		return TRUE;

	return FALSE;
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
	g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_NOT_SUPPORTED, "quirk key not supported");
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
	fu_device_add_internal_flag(FU_DEVICE(self), FU_DEVICE_INTERNAL_FLAG_USE_PARENT_FOR_OPEN);
	fu_device_add_icon(FU_DEVICE(self), "input-touchpad");
	fu_device_add_protocol(FU_DEVICE(self), "tw.com.emc.elantp.haptic");
	fu_device_set_name(FU_DEVICE(self), "Elan HapticPad EEPROM");
	fu_device_set_summary(FU_DEVICE(self), "Elan HapticPad EEPROM");
	fu_device_set_logical_id(FU_DEVICE(self), "eeprom");
	fu_device_set_version_format(FU_DEVICE(self), FWUPD_VERSION_FORMAT_HEX);
	fu_device_set_priority(FU_DEVICE(self), 1); /* better than i2c */
}

static void
fu_elantp_hid_haptic_device_finalize(GObject *object)
{
	G_OBJECT_CLASS(fu_elantp_hid_haptic_device_parent_class)->finalize(object);
}

static void
fu_elantp_hid_haptic_device_class_init(FuElantpHidHapticDeviceClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS(klass);
	FuDeviceClass *klass_device = FU_DEVICE_CLASS(klass);
	object_class->finalize = fu_elantp_hid_haptic_device_finalize;
	klass_device->to_string = fu_elantp_hid_haptic_device_to_string;
	klass_device->attach = fu_elantp_hid_haptic_device_attach;
	klass_device->set_quirk_kv = fu_elantp_hid_haptic_device_set_quirk_kv;
	klass_device->setup = fu_elantp_hid_haptic_device_setup;
	klass_device->reload = fu_elantp_hid_haptic_device_setup;
	klass_device->write_firmware = fu_elantp_hid_haptic_device_write_firmware;
	klass_device->prepare_firmware = fu_elantp_hid_haptic_device_prepare_firmware;
	klass_device->probe = fu_elantp_hid_haptic_device_probe;
	klass_device->set_progress = fu_elantp_hid_haptic_device_set_progress;
}

FuElantpHidHapticDevice *
fu_elantp_haptic_device_new(FuDevice *device)
{
	FuElantpHidHapticDevice *self;
	
	self = g_object_new(FU_TYPE_ELANTP_HID_HAPTIC_DEVICE, NULL);
	return FU_ELANTP_HID_HAPTIC_DEVICE(self);
}
