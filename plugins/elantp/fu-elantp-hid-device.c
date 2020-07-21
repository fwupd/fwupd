/*
 * Copyright (C) 2020 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include <linux/hidraw.h>
#include <linux/input.h>

#include "fu-elantp-common.h"
#include "fu-elantp-firmware.h"
#include "fu-elantp-hid-device.h"

#include "fu-chunk.h"

struct _FuElantpHidDevice {
	FuUdevDevice		 parent_instance;
	guint16			 ic_page_count;
	guint16			 iap_ctrl;
	guint16			 iap_password;
	guint16			 module_id;
};

#define ELANTP_DELAY_COMPLETE		1200	/* ms */
#define ELANTP_DELAY_RESET		30	/* ms */
#define ELANTP_DELAY_UNLOCK		100	/* ms */
#define ELANTP_DELAY_WRITE_BLOCK	35	/* ms */

G_DEFINE_TYPE (FuElantpHidDevice, fu_elantp_hid_device, FU_TYPE_UDEV_DEVICE)

static void
fu_elantp_hid_device_to_string (FuDevice *device, guint idt, GString *str)
{
	FuElantpHidDevice *self = FU_ELANTP_HID_DEVICE (device);
	fu_common_string_append_kx (str, idt, "ModuleId", self->module_id);
	fu_common_string_append_kx (str, idt, "IcPageCount", self->ic_page_count);
	fu_common_string_append_kx (str, idt, "EapCtrl", self->iap_ctrl);
}

static gboolean
fu_elantp_hid_device_probe (FuUdevDevice *device, GError **error)
{
	/* check is valid */
	if (g_strcmp0 (fu_udev_device_get_subsystem (device), "hidraw") != 0) {
		g_set_error (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_NOT_SUPPORTED,
			     "is not correct subsystem=%s, expected hidraw",
			     fu_udev_device_get_subsystem (device));
		return FALSE;
	}

	/* set the physical ID */
	return fu_udev_device_set_physical_id (device, "hid", error);
}

static gboolean
fu_elantp_hid_device_send_cmd (FuElantpHidDevice *self,
			       guint8 *tx, gsize txsz,
			       guint8 *rx, gsize rxsz,
			       GError **error)
{
	g_autofree guint8 *buf = NULL;

	if (!fu_udev_device_ioctl (FU_UDEV_DEVICE (self),
				   HIDIOCSFEATURE(txsz), tx,
				   NULL, error))
		return FALSE;
	if (rxsz == 0)
		return TRUE;

	/* GetFeature */
	buf = g_malloc0 (rxsz + 1);
	buf[0] = tx[0]; /* report number */
	if (!fu_udev_device_ioctl (FU_UDEV_DEVICE (self),
				   HIDIOCGFEATURE(rxsz + 3), buf,
				   NULL, error))
		return FALSE;

	/* success */
	memcpy (rx, buf + 0x3, rxsz);
	return TRUE;
}

static gboolean
fu_elantp_hid_device_read_cmd (FuElantpHidDevice *self, guint16 reg,
			       guint8 *rx, gsize rxsz, GError **error)
{
	guint8 buf[5] = { 0x0d, 0x05, 0x03 };
	fu_common_write_uint16 (buf + 0x3, reg, G_LITTLE_ENDIAN);
	if (g_getenv ("FWUPD_ELANTP_VERBOSE") != NULL)
		fu_common_dump_raw (G_LOG_DOMAIN, "ReadCmd", buf, sizeof(buf));
	return fu_elantp_hid_device_send_cmd (self, buf, sizeof(buf), rx, rxsz, error);
}

static gint
fu_elantp_hid_device_write_cmd (FuElantpHidDevice *self,
				guint16 reg, guint16 cmd,
				GError **error)
{
	guint8 buf[5] = { 0x0d };
	fu_common_write_uint16 (buf + 0x1, reg, G_LITTLE_ENDIAN);
	fu_common_write_uint16 (buf + 0x3, cmd, G_LITTLE_ENDIAN);
	if (g_getenv ("FWUPD_ELANTP_VERBOSE") != NULL)
		fu_common_dump_raw (G_LOG_DOMAIN, "WriteCmd", buf, sizeof(buf));

	return fu_elantp_hid_device_send_cmd (self, buf, sizeof(buf), NULL, 0, error);
}

static gboolean
fu_elantp_hid_device_ensure_iap_ctrl (FuElantpHidDevice *self, GError **error)
{
	guint8 buf[2] = { 0x0 };
	if (!fu_elantp_hid_device_read_cmd (self, ETP_CMD_I2C_IAP_CTRL, buf, sizeof(buf), error)) {
		g_prefix_error (error, "failed to read IAPControl: ");
		return FALSE;
	}
	self->iap_ctrl = fu_common_read_uint16 (buf, G_LITTLE_ENDIAN);

	/* in bootloader mode? */
	if ((self->iap_ctrl & ETP_I2C_MAIN_MODE_ON) == 0)
		fu_device_add_flag (FU_DEVICE (self), FWUPD_DEVICE_FLAG_IS_BOOTLOADER);
	else
		fu_device_remove_flag (FU_DEVICE (self), FWUPD_DEVICE_FLAG_IS_BOOTLOADER);

	return TRUE;
}

static gboolean
fu_elantp_hid_device_setup (FuDevice *device, GError **error)
{
	FuElantpHidDevice *self = FU_ELANTP_HID_DEVICE (device);
	FuUdevDevice *udev_device = FU_UDEV_DEVICE (device);
	gboolean is_new_pattern;
	guint16 fwver;
	guint16 tmp;
	guint8 buf[2] = { 0x0 };
	guint8 ic_type;
	g_autofree gchar *instance_id1 = NULL;
	g_autofree gchar *instance_id_ic_type = NULL;
	g_autofree gchar *version_bl = NULL;
	g_autofree gchar *version = NULL;

	/* get current firmware version */
	if (!fu_elantp_hid_device_read_cmd (self,
					    ETP_CMD_I2C_FW_VERSION,
					    buf, sizeof(buf), error)) {
		g_prefix_error (error, "failed to read fw version: ");
		return FALSE;
	}
	fwver = fu_common_read_uint16 (buf, G_LITTLE_ENDIAN);
	version = fu_common_version_from_uint16 (fwver, FWUPD_VERSION_FORMAT_HEX);
	fu_device_set_version (device, version);

	/* get EAP firmware version */
	is_new_pattern = fu_device_has_custom_flag (FU_DEVICE (self), "iap-version-2");
	if (!fu_elantp_hid_device_read_cmd (self,
					    is_new_pattern ? ETP_CMD_I2C_IAP_VERSION_2 : ETP_CMD_I2C_IAP_VERSION,
					    buf, sizeof(buf), error)) {
		g_prefix_error (error, "failed to read bootloader version: ");
		return FALSE;
	}
	fwver = fu_common_read_uint16 (buf, G_LITTLE_ENDIAN);
	version_bl = fu_common_version_from_uint16 (fwver, FWUPD_VERSION_FORMAT_HEX);
	fu_device_set_version_bootloader (device, version_bl);

	/* get module ID */
	if (!fu_elantp_hid_device_read_cmd (self,
					    ETP_CMD_GET_MODULE_ID,
					    buf, sizeof(buf), error)) {
		g_prefix_error (error, "failed to read module ID: ");
		return FALSE;
	}
	self->module_id = fu_common_read_uint16 (buf, G_LITTLE_ENDIAN);

	/* define the extra instance IDs */
	instance_id1 = g_strdup_printf ("HIDRAW\\VEN_%04X&DEV_%04X&MOD_%04X",
					fu_udev_device_get_vendor (udev_device),
					fu_udev_device_get_model (udev_device),
					self->module_id);
	fu_device_add_instance_id (device, instance_id1);

	/* get OSM version */
	if (!fu_elantp_hid_device_read_cmd (self, ETP_CMD_I2C_OSM_VERSION, buf, sizeof(buf), error)) {
		g_prefix_error (error, "failed to read OSM version: ");
		return FALSE;
	}
	tmp = fu_common_read_uint16 (buf, G_LITTLE_ENDIAN);
	if (tmp == ETP_CMD_I2C_OSM_VERSION || tmp == 0xFFFF) {
		if (!fu_elantp_hid_device_read_cmd (self, ETP_CMD_I2C_IAP_ICBODY, buf, sizeof(buf), error)) {
			g_prefix_error (error, "failed to read IC body: ");
			return FALSE;
		}
		ic_type = fu_common_read_uint16 (buf, G_LITTLE_ENDIAN) & 0xFF;
	} else {
		ic_type = (tmp >> 8) & 0xFF;
	}
	instance_id_ic_type = g_strdup_printf ("ELANTP\\ICTYPE_%02X", ic_type);
	fu_device_add_instance_id (device, instance_id_ic_type);

	/* no quirk entry */
	if (self->ic_page_count == 0x0) {
		g_set_error (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_NOT_SUPPORTED,
			     "no page count for ELANTP\\ICTYPE_%02X",
			     ic_type);
		return FALSE;
	}
	fu_device_set_firmware_size (device, self->ic_page_count * FW_PAGE_SIZE);

	/* is in bootloader mode */
	if (!fu_elantp_hid_device_ensure_iap_ctrl (self, error))
		return FALSE;

	/* success */
	return TRUE;
}

static FuFirmware *
fu_elantp_hid_device_prepare_firmware (FuDevice *device,
				       GBytes *fw,
				       FwupdInstallFlags flags,
				       GError **error)
{
	FuElantpHidDevice *self = FU_ELANTP_HID_DEVICE (device);
	guint16 module_id;
	g_autoptr(FuFirmware) firmware = fu_elantp_firmware_new ();

	/* check size */
	if (g_bytes_get_size (fw) < fu_device_get_firmware_size_min (device)) {
		g_set_error (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_INVALID_FILE,
			     "firmware too small, got 0x%x, expected >= 0x%x",
			     (guint) g_bytes_get_size (fw),
			     (guint) fu_device_get_firmware_size_min (device));
		return NULL;
	}
	if (g_bytes_get_size (fw) > fu_device_get_firmware_size_max (device)) {
		g_set_error (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_INVALID_FILE,
			     "firmware too large, got 0x%x, expected <= 0x%x",
			     (guint) g_bytes_get_size (fw),
			     (guint) fu_device_get_firmware_size_max (device));
		return NULL;
	}

	/* check is compatible with hardware */
	fu_device_set_status (device, FWUPD_STATUS_DECOMPRESSING);
	if (!fu_firmware_parse (firmware, fw, flags, error))
		return NULL;
	module_id = fu_elantp_firmware_get_module_id (FU_ELANTP_FIRMWARE (firmware));
	if (self->module_id != module_id) {
		g_set_error (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_INVALID_FILE,
			     "firmware incompatible, got 0x%04x, expected 0x%04x",
			     module_id, self->module_id);
		return NULL;
	}

	/* success */
	return g_steal_pointer (&firmware);
}

static gboolean
fu_elantp_hid_device_write_firmware (FuDevice *device,
				     FuFirmware *firmware,
				     FwupdInstallFlags flags,
				     GError **error)
{
	FuElantpHidDevice *self = FU_ELANTP_HID_DEVICE (device);
	FuElantpFirmware *firmware_elantp = FU_ELANTP_FIRMWARE (firmware);
	gsize bufsz = 0;
	guint16 checksum = 0;
	guint16 checksum_device = 0;
	guint16 iap_addr;
	const guint8 *buf;
	guint8 csum_buf[2] = { 0x0 };
	g_autoptr(GBytes) fw = NULL;
	g_autoptr(GPtrArray) chunks = NULL;

	/* simple image */
	fw = fu_firmware_get_image_default_bytes (firmware, error);
	if (fw == NULL)
		return FALSE;

	/* write each block */
	fu_device_set_status (device, FWUPD_STATUS_DEVICE_WRITE);
	buf = g_bytes_get_data (fw, &bufsz);
	iap_addr = fu_elantp_firmware_get_iap_addr (firmware_elantp);
	chunks = fu_chunk_array_new (buf + iap_addr, bufsz - iap_addr, 0x0, 0x0, FW_PAGE_SIZE);
	for (guint i = 0; i < chunks->len; i++) {
		FuChunk *chk = g_ptr_array_index (chunks, i);
		guint16 csum_tmp = fu_elantp_calc_checksum (chk->data, chk->data_sz);
		guint8 blk[FW_PAGE_SIZE + 3];

		/* write block */
		blk[0] = 0x0B; /* report ID */
		memcpy (blk + 1, chk->data, chk->data_sz);
		fu_common_write_uint16 (blk + chk->data_sz + 1, csum_tmp, G_LITTLE_ENDIAN);

		if (!fu_elantp_hid_device_send_cmd (self, blk, sizeof (blk), NULL, 0, error))
			return FALSE;
		g_usleep (ELANTP_DELAY_WRITE_BLOCK * 1000);
		if (!fu_elantp_hid_device_ensure_iap_ctrl (self, error))
			return FALSE;
		if (self->iap_ctrl & (ETP_FW_IAP_PAGE_ERR | ETP_FW_IAP_INTF_ERR)) {
			g_set_error (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_WRITE,
				     "bootloader reports failed write: 0x%x",
				     self->iap_ctrl);
			return FALSE;
		}

		/* update progress */
		checksum += csum_tmp;
		fu_device_set_progress_full (device, (gsize) i, (gsize) chunks->len);
	}

	/* verify the written checksum */
	fu_device_set_status (device, FWUPD_STATUS_DEVICE_VERIFY);
	if (!fu_elantp_hid_device_read_cmd (self, ETP_CMD_I2C_IAP_CHECKSUM,
					    csum_buf, sizeof(csum_buf), error))
		return FALSE;
	checksum_device = fu_common_read_uint16 (csum_buf, G_LITTLE_ENDIAN);
	if (checksum != checksum_device) {
		g_set_error (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_WRITE,
			     "checksum failed 0x%04x != 0x%04x",
			     checksum, checksum_device);
		return FALSE;
	}

	/* wait for a reset */
	fu_device_set_progress (device, 0);
	fu_device_set_status (device, FWUPD_STATUS_DEVICE_RESTART);
	g_usleep (ELANTP_DELAY_COMPLETE * 1000);
	return TRUE;
}

static gboolean
fu_elantp_hid_device_detach (FuDevice *device, GError **error)
{
	FuElantpHidDevice *self = FU_ELANTP_HID_DEVICE (device);

	/* sanity check */
	if (fu_device_has_flag (device, FWUPD_DEVICE_FLAG_IS_BOOTLOADER)) {
		g_debug ("already in bootloader mode, skipping");
		return TRUE;
	}

	g_debug ("in bootloader mode, reset IC");
	fu_device_set_status (device, FWUPD_STATUS_DEVICE_RESTART);
	if (!fu_elantp_hid_device_write_cmd (self,
					     ETP_CMD_I2C_IAP_RESET,
					     ETP_I2C_IAP_RESET,
					     error))
		return FALSE;
	g_usleep (ELANTP_DELAY_RESET * 1000);
	if (!fu_elantp_hid_device_write_cmd (self,
					     ETP_CMD_I2C_IAP,
					     self->iap_password,
					     error))
		return FALSE;
	g_usleep (ELANTP_DELAY_UNLOCK * 1000);
	if (!fu_elantp_hid_device_ensure_iap_ctrl (self, error))
		return FALSE;
	if ((self->iap_ctrl & ETP_FW_IAP_CHECK_PW) == 0) {
		g_set_error_literal (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_WRITE,
				     "unexpected bootloader password");
		return FALSE;
	}

	/* success */
	return TRUE;
}

static gboolean
fu_elantp_hid_device_attach (FuDevice *device, GError **error)
{
	FuElantpHidDevice *self = FU_ELANTP_HID_DEVICE (device);

	/* sanity check */
	if (!fu_device_has_flag (device, FWUPD_DEVICE_FLAG_IS_BOOTLOADER)) {
		g_debug ("already in runtime mode, skipping");
		return TRUE;
	}

	/* reset back to runtime */
	fu_device_set_status (device, FWUPD_STATUS_DEVICE_RESTART);
	if (!fu_elantp_hid_device_write_cmd (self, ETP_CMD_I2C_IAP_RESET, ETP_I2C_IAP_RESET, error))
		return FALSE;
	g_usleep (ELANTP_DELAY_RESET * 1000);
	if (!fu_elantp_hid_device_write_cmd (self, ETP_CMD_I2C_IAP_RESET, ETP_I2C_ENABLE_REPORT, error)) {
		g_prefix_error (error, "cannot enable TP report: ");
		return FALSE;
	}
	if (!fu_elantp_hid_device_write_cmd (self, 0x0306, 0x003, error)) {
		g_prefix_error (error, "cannot switch to TP PTP mode: ");
		return FALSE;
	}
	if (!fu_elantp_hid_device_ensure_iap_ctrl (self, error))
		return FALSE;

	/* success */
	return TRUE;
}

static gboolean
fu_elantp_hid_device_set_quirk_kv (FuDevice *device,
				   const gchar *key,
				   const gchar *value,
				   GError **error)
{
	FuElantpHidDevice *self = FU_ELANTP_HID_DEVICE (device);
	if (g_strcmp0 (key, "ElantpIcPageCount") == 0) {
		guint64 tmp = fu_common_strtoull (value);
		if (tmp > 0xffff) {
			g_set_error_literal (error,
					     G_IO_ERROR,
					     G_IO_ERROR_NOT_SUPPORTED,
					     "ElantpIcPageCount only supports "
					     "values <= 0xffff");
			return FALSE;
		}
		self->ic_page_count = (guint16) tmp;
		return TRUE;
	}
	if (g_strcmp0 (key, "ElantpIapPassword") == 0) {
		guint64 tmp = fu_common_strtoull (value);
		if (tmp > 0xffff) {
			g_set_error_literal (error,
					     G_IO_ERROR,
					     G_IO_ERROR_NOT_SUPPORTED,
					     "ElantpIapPassword only supports "
					     "values <= 0xffff");
			return FALSE;
		}
		self->iap_password = (guint16) tmp;
		return TRUE;
	}
	g_set_error_literal (error,
			     G_IO_ERROR,
			     G_IO_ERROR_NOT_SUPPORTED,
			     "quirk key not supported");
	return FALSE;
}

static void
fu_elantp_hid_device_init (FuElantpHidDevice *self)
{
	fu_device_add_flag (FU_DEVICE (self), FWUPD_DEVICE_FLAG_INTERNAL);
	fu_device_add_flag (FU_DEVICE (self), FWUPD_DEVICE_FLAG_UPDATABLE);
	fu_device_set_summary (FU_DEVICE (self), "Elan Touchpad");
	fu_device_add_icon (FU_DEVICE (self), "input-touchpad");
	fu_device_set_protocol (FU_DEVICE (self), "tw.com.emc.elantp");
	fu_device_set_version_format (FU_DEVICE (self), FWUPD_VERSION_FORMAT_HEX);
	fu_udev_device_set_flags (FU_UDEV_DEVICE (self),
				  FU_UDEV_DEVICE_FLAG_OPEN_READ |
				  FU_UDEV_DEVICE_FLAG_OPEN_WRITE |
				  FU_UDEV_DEVICE_FLAG_OPEN_NONBLOCK);
}

static void
fu_elantp_hid_device_finalize (GObject *object)
{
	G_OBJECT_CLASS (fu_elantp_hid_device_parent_class)->finalize (object);
}

static void
fu_elantp_hid_device_class_init (FuElantpHidDeviceClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	FuDeviceClass *klass_device = FU_DEVICE_CLASS (klass);
	FuUdevDeviceClass *klass_udev_device = FU_UDEV_DEVICE_CLASS (klass);
	object_class->finalize = fu_elantp_hid_device_finalize;
	klass_device->to_string = fu_elantp_hid_device_to_string;
	klass_device->attach = fu_elantp_hid_device_attach;
	klass_device->detach = fu_elantp_hid_device_detach;
	klass_device->set_quirk_kv = fu_elantp_hid_device_set_quirk_kv;
	klass_device->setup = fu_elantp_hid_device_setup;
	klass_device->write_firmware = fu_elantp_hid_device_write_firmware;
	klass_device->prepare_firmware = fu_elantp_hid_device_prepare_firmware;
	klass_udev_device->probe = fu_elantp_hid_device_probe;
}
