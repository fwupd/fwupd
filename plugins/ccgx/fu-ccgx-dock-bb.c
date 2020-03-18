/*
 * Copyright (C) 2020 Cypress Semiconductor Corporation.
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include <string.h>
#include "fu-common-version.h"
#include "fu-usb-device.h"
#include "fwupd-error.h"
#include "fu-ccgx-dock-bb.h"
#include "fu-ccgx-common.h"
#include "fu-ccgx-hpi.h"
#include "fu-ccgx-hid.h"
#include "fu-ccgx-cyacd-firmware.h"
#include "fu-ccgx-cyacd-file.h"

/* i2c slave address for pd device */
#define PD_I2C_SLAVE_ADDRESS 0x08

/* hid Interface number */
#define USB_HID_INF_NUM 1

/* usb i2c interface number */
#define USB_I2C_INF_NUM 0

/* gen2 dock model name */
#define CCGX_GEN2_DOCK_MODEL_NAME "Gen2"

struct _FuCcgxDockBb {
	FuUsbDevice	parent_instance;
	guint16	usb_inf_num;			/* usb Interface Number */
	CyHPIHandle	*hpi_handle;			/* hpi handle for pd device i2c */
	PDDeviceData	pd_device_data;			/* pd device Information data */
	DMDevice	dm_device;			/* device manager device type */
	guint16	quirks_silicon_id;		/* silicon id in quirks */
	guint16	quirks_fw_app_type;		/* fw application type in quirks */
	gboolean	flag_dm_has_child;		/* flag that indicate device manager has child */
	FWImageType	fw_image_type;			/* firmware Image type */
	gboolean	fw_primary_update_only;		/* only update primary image */
	gchar*		fw_update_message;		/* update message */
	gchar*		fw_update_message_primary;	/* update message postfix for primary */
	gchar*		fw_update_message_backup;	/* update message postfix for backup */
	gchar*		model_name;			/* dock model name */
	gboolean	fw_update_success;		/* fw update success flag */
	gboolean	device_removed;			/* device is removed */
	gboolean	claimed_interface;		/* usb interface claimed */
};

G_DEFINE_TYPE (FuCcgxDockBb, fu_ccgx_dock_bb, FU_TYPE_USB_DEVICE)

/* configure HPI handle through I2C and get data from device */
static gboolean
fu_ccgx_dock_bb_pd_i2c_configure (FuDevice *device, GError **error)
{
	FuCcgxDockBb *self = FU_CCGX_DOCK_BB (device);
	CyHPIHandle  *hpi_handle = self->hpi_handle;
	DMDevice dm_device = self->dm_device;
	guint16 usb_inf_num = self->usb_inf_num;
	guint8 slave_address = PD_I2C_SLAVE_ADDRESS;

	/* setup I2C */
	if (!fu_ccgx_hpi_cmd_setup (FU_DEVICE(device), hpi_handle, dm_device, usb_inf_num, slave_address, error)) {
		return FALSE;
	}

	/* get device data from device */
	if (!fu_ccgx_hpi_cmd_get_device_data (FU_DEVICE(device),hpi_handle, &self->pd_device_data, error)) {
		return FALSE;
	}

	/* check Silicon ID */
	if (self->pd_device_data.silicon_id != self->quirks_silicon_id) {
		g_set_error(error,FWUPD_ERROR,
			FWUPD_ERROR_NOT_SUPPORTED,
			"silicon id mismatch");
		g_warning("silicon id mismatch 0x%04X / 0x%04X", self->pd_device_data.silicon_id,self->quirks_silicon_id);
		return FALSE;
	}

	if (self->pd_device_data.fw_mode != FW_MODE_BOOT) {
		/* check version type */
		if (self->pd_device_data.current_version.ver.type != self->quirks_fw_app_type) {
			g_set_error(error,FWUPD_ERROR,
				FWUPD_ERROR_NOT_SUPPORTED,
				"application type mismatch");
			g_warning("applicatin type mismatch 0x%02X / 0x%02X", self->pd_device_data.current_version.ver.type,self->quirks_fw_app_type);
			return FALSE;
		}
	}
        return TRUE;
}

/* write firmware for pd i2c device */
static gboolean
fu_ccgx_dock_bb_pd_i2c_write_fw (FuDevice *device,
				 guint8* fw_buffer,
				 gsize fw_size,
				 FwupdInstallFlags flags,
				 GError **error)
{
	FuCcgxDockBb *self = FU_CCGX_DOCK_BB (device);
	CyHPIHandle* hpi_handle = self->hpi_handle;
	guint32 fw_pos = 0;
	g_autofree guint8* row_buffer = NULL;
	guint16 row_num = 0;
	guint16 row_size = 0;
	guint8* row_data = NULL;
	FWMode update_fw_mode = 0;
	CyacdFileInfo cyacd_file_info = {0};
	CyacdFileHandle cyacd_handle_array[CYACD_HANDLE_MAX_COUNT] = {0};
	CyacdFileHandle* cyacd_handle = NULL;
	guint32 handle_count = 0;
	guint32 index = 0;
	CCGxMetaData* metadata = NULL;
	PDFWAppVersion update_fw_version = {0};
	g_autofree gchar *update_str_version = NULL;
	gsize update_fw_size = 0;

	row_buffer = g_malloc0 (CYACD_ROW_BUFFER_SIZE);
	g_return_val_if_fail (row_buffer != NULL, FALSE);

	handle_count =  fu_ccgx_cyacd_file_init_handle(cyacd_handle_array,
						CYACD_HANDLE_MAX_COUNT, fw_buffer, fw_size);

	if (self->fw_image_type != FW_IMAGE_TYPE_DUAL_SYMMETRIC &&
		 self->fw_image_type != FW_IMAGE_TYPE_DUAL_ASYMMETRIC) {
			g_set_error(error,FWUPD_ERROR,
				    FWUPD_ERROR_NOT_SUPPORTED,
				   "not supported fw image");
		return FALSE;
	}

	if (handle_count <= 0) {
		g_set_error(error,FWUPD_ERROR,
			   FWUPD_ERROR_NOT_SUPPORTED,
			   "invalid firmware type");
		return FALSE;
	}

	for (index = 0; index < handle_count ; index++) {
		/* get cyacd handle */
		cyacd_handle = &cyacd_handle_array [index];
		update_fw_size = cyacd_handle->buffer_size;

		/* parse cyacd data */
		if (!fu_ccgx_cyacd_file_parse (cyacd_handle, &cyacd_file_info)) {
			g_set_error (error,FWUPD_ERROR,
				     FWUPD_ERROR_NOT_SUPPORTED,
				     "cyacd parsing error");
			g_warning ("cyacd parsing error");
			return FALSE;
		}

		/* check silicon ID */
		if (self->pd_device_data.silicon_id != cyacd_file_info.silicon_id) {
			g_set_error (error,FWUPD_ERROR,
				     FWUPD_ERROR_NOT_SUPPORTED,
				     "silicon id mismatch");
			g_warning ("silicon id mismatch 0x%X / 0x%X ", self->pd_device_data.silicon_id,
				   cyacd_file_info.silicon_id);
			return FALSE;
		}

		/* check application version type */
		if (self->pd_device_data.fw_mode != FW_MODE_BOOT) {
			if (self->pd_device_data.current_version.ver.type != cyacd_file_info.app_version.ver.type) {
				g_set_error (error, FWUPD_ERROR,
					     FWUPD_ERROR_NOT_SUPPORTED,
					     "application type mismatch");
				g_warning ("application	type mismatch 0x%02X / 0x%02X", 
					   self->pd_device_data.current_version.ver.type ,
					   cyacd_file_info.app_version.ver.type);
				return FALSE;
			}
		}

		if (self->fw_image_type == FW_IMAGE_TYPE_DUAL_SYMMETRIC || self->fw_image_type == FW_IMAGE_TYPE_DUAL_ASYMMETRIC) {
			if (self->pd_device_data.fw_mode == FW_MODE_FW1)
				update_fw_mode = FW_MODE_FW2;
			else if (self->pd_device_data.fw_mode == FW_MODE_FW2)
				update_fw_mode = FW_MODE_FW1;
			else if (self->pd_device_data.fw_mode == FW_MODE_BOOT)
				update_fw_mode = cyacd_file_info.fw_mode;
			else {
				g_set_error (error, FWUPD_ERROR,
					     FWUPD_ERROR_NOT_SUPPORTED,
					     "not supported fw mode 0x%x", (guint32)self->pd_device_data.fw_mode);
				g_warning ("not supported fw mode 0x%x", (guint32)self->pd_device_data.fw_mode);
				return FALSE;;
			}

			if (cyacd_file_info.fw_mode != update_fw_mode ||
			    (self->fw_primary_update_only == TRUE && self->pd_device_data.fw_mode  == FW_MODE_FW2)) {
				if (handle_count > 1)
					continue; /* get next handle */
				else
					break;
			}
		}

		g_debug ("===== Update FW file Info =====");
		g_debug ("  Silicon ID : 0x%X", cyacd_file_info.silicon_id);
		g_debug ("  FW Mode :  FW%u", (guint32)cyacd_file_info.fw_mode);

		if (self->fw_image_type == FW_IMAGE_TYPE_DUAL_SYMMETRIC)
			g_debug ("  FW IMG :  DUAL SYMMETRIC");
		else if (self->fw_image_type == FW_IMAGE_TYPE_DUAL_ASYMMETRIC)
			g_debug ("  FW IMG :  DUAL ASYMMETRIC");

		g_debug ("  Version : %u.%u.%u /  0x%02X(%c%c)", 
			(guint32)cyacd_file_info.app_version.ver.major,
			(guint32) cyacd_file_info.app_version.ver.minor,
			(guint32)cyacd_file_info.app_version.ver.build,
			cyacd_file_info.app_version.ver.type,
			((cyacd_file_info.app_version.ver.type>>8) & 0xff),
			((cyacd_file_info.app_version.ver.type) & 0xff));

		/* enter FW Mode */
		if (!fu_ccgx_hpi_cmd_enter_flash_mode (device, hpi_handle, error)) {
			g_warning("enter flash mode error");
			return FALSE;
		}

		g_usleep (HPI_CMD_ENTER_FLASH_MODE_DELAY_US); /* wait 10 msec */

		/* set up meta data row */
		if (update_fw_mode == FW_MODE_FW1)
			row_num = self->pd_device_data.fw1_meta_row_num;
		else
			row_num = self->pd_device_data.fw2_meta_row_num;

		row_data = &row_buffer[4];

		/* read meta data */
		row_size = self->pd_device_data.fw_row_size;

		fu_device_set_status (device,FWUPD_STATUS_DEVICE_READ);

		if (!fu_ccgx_hpi_cmd_read_flash (device, hpi_handle, row_num, row_data, row_size, error)) {
			g_warning("fw meta data read error ");
			return FALSE;
		}

		/* get meta data */
		metadata = (CCGxMetaData*)&row_data[self->pd_device_data.fw_meta_offset];

		/* clear meta data Valid */
		metadata->metadata_valid = 0x00;

		/* write meta data again */
		fu_device_set_status (device,FWUPD_STATUS_DEVICE_ERASE);

		if (!fu_ccgx_hpi_cmd_write_flash (device, hpi_handle, row_num, row_data, row_size,error)) {
			g_warning ("fw meta data write error");
			return FALSE;
		}

		/* read flash data */
		fu_device_set_status (device,FWUPD_STATUS_DEVICE_WRITE);

		g_debug ("Writing Firmware ...");

		while (fu_ccgx_cyacd_file_read_row (cyacd_handle, row_buffer,CYACD_ROW_BUFFER_SIZE)) {
			row_num = *((guint16*)(&row_buffer[0]));
			row_size = *((guint16*)(&row_buffer[2]));
			row_data = &row_buffer[4];

			/* write flash data */
			if (!fu_ccgx_hpi_cmd_write_flash (device, hpi_handle, row_num, row_data, row_size, error)) {
				g_warning ("fw row data write error at %d row", (gint32)row_num);
				return FALSE;
			}

			/* update process */
			fw_pos = fu_ccgx_cyacd_file_get_pos (cyacd_handle);
			fu_device_set_progress_full ( device, fw_pos, update_fw_size );
		}

		/* validate fw */
		fu_device_set_status(device,FWUPD_STATUS_DEVICE_VERIFY);
		if (!fu_ccgx_hpi_cmd_validate_fw (device, hpi_handle, update_fw_mode,error)) {
			g_warning ("fw validate error");
			return FALSE;
		}

		fw_pos = fu_ccgx_cyacd_file_get_pos (cyacd_handle);
		fu_device_set_progress_full (device, fw_pos, update_fw_size);

		break;
	}

	update_fw_version.val = cyacd_file_info.app_version.val;
	update_str_version = g_strdup_printf ("%u.%u.%u", 
						(guint32)update_fw_version.ver.major,
						(guint32)update_fw_version.ver.minor,
						(guint32)update_fw_version.ver.build);
	g_debug ("Update version %s", update_str_version);

	/* update version of device */
	fu_device_set_version (FU_DEVICE (self), update_str_version);
	return TRUE;
}

static gboolean
fu_ccgx_dock_bb_write_fw (FuDevice *device,
			  FuFirmware *firmware,
			  FwupdInstallFlags flags,
			  GError **error)
{
	FuCcgxDockBb *self = FU_CCGX_DOCK_BB (device);
	const guint8* fw_buffer = NULL;
	gsize fw_size = 0;
	g_autoptr(GBytes) fw = NULL;

	if (self->dm_device != DM_DEVICE_PD_I2C) {
		g_set_error (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_NOT_SUPPORTED,
			     "not supported device type");
		return FALSE;
	}

	/* get default Image */
	fw = fu_firmware_get_image_default_bytes (firmware, error);
	if (fw == NULL)
		return FALSE;

	/* get firmware buffer and size */
	fw_buffer = g_bytes_get_data (fw, &fw_size);
	if (fw_size <= 0) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_NOT_SUPPORTED,
			    "firmware size error");
		return FALSE;
	}
	if (!fu_ccgx_dock_bb_pd_i2c_write_fw (device, fw_buffer, fw_size, flags, error)) {
		g_prefix_error (error, "write_fw error: ");
		self->fw_update_success = FALSE;
		return FALSE;
	}
	self->fw_update_success = TRUE;
	return TRUE;
}

static gboolean
fu_ccgx_dock_bb_set_quirk_kv (FuDevice *device,
			      const gchar *key,
			      const gchar *value,
			      GError **error)
{
	FuCcgxDockBb *self = FU_CCGX_DOCK_BB (device);

	if (g_strcmp0 (key, "DeviceSiliconID") == 0) {
		guint64 tmp = fu_common_strtoull (value);
		if (tmp < G_MAXUINT16) {
			self->quirks_silicon_id = tmp;
			return TRUE;
		}
		g_set_error_literal (error,
				     G_IO_ERROR,
				     G_IO_ERROR_INVALID_DATA,
				     "invalid DeviceSiliconID");
		return FALSE;
	}
	if (g_strcmp0 (key, "DeviceFWAppType") == 0) {
		guint64 tmp = fu_common_strtoull (value);
		if (tmp < G_MAXUINT16) {
			self->quirks_fw_app_type = tmp;
			return TRUE;
		}
		g_set_error_literal (error,
				     G_IO_ERROR,
				     G_IO_ERROR_INVALID_DATA,
				     "invalid DeviceFWAppType");
		return FALSE;
	}
	if (g_strcmp0 (key, "UpdateMessage") == 0) {
		self->fw_update_message = g_strdup(value);
		return TRUE;
	}
	if (g_strcmp0 (key, "UpdateMessagePrimary") == 0) {
		self->fw_update_message_primary = g_strdup(value);
		return TRUE;
	}
	if (g_strcmp0 (key, "UpdateMessageBackup") == 0) {
		self->fw_update_message_backup = g_strdup(value);
		return TRUE;
	}
	if (g_strcmp0 (key, "ModelName") == 0) {
		self->model_name = g_strdup(value);
		return TRUE;
	}
	return FALSE;
}

static gboolean
fu_ccgx_dock_bb_probe (FuDevice *device, GError **error)
{
	FuCcgxDockBb *self = FU_CCGX_DOCK_BB (device);
	g_autofree gchar *devid_bb = NULL;

	/* get vid and pid of device */
	devid_bb = g_strdup_printf ("USB\\VID_%04X&PID_%04X&bb", 
				   (guint) fu_usb_device_get_vid (FU_USB_DEVICE (device)),
				   (guint) fu_usb_device_get_pid (FU_USB_DEVICE (device)));

	/* this will trigger setting up all the quirks */
	fu_device_add_instance_id (device, devid_bb);

	self->dm_device = DM_DEVICE_NONE;

	/* set device type and fw image type according to custom flag in quirk data */
	if (fu_device_has_custom_flag (FU_DEVICE(device),"cy-device-external-bb")) {
		self->dm_device = DM_DEVICE_EXTERNAL_BB;
		self->usb_inf_num = USB_HID_INF_NUM;
	} else if (fu_device_has_custom_flag (FU_DEVICE(device),"cy-device-pd-i2c")) {
		self->dm_device = DM_DEVICE_PD_I2C;
		self->usb_inf_num = USB_I2C_INF_NUM;
	} else {
		g_set_error (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_NOT_SUPPORTED,
			     "not supported device");
		return FALSE;
	}

	self->fw_image_type = FW_IMAGE_TYPE_NONE;
	if (self->dm_device  == DM_DEVICE_PD_I2C) {
		if (fu_device_has_custom_flag (FU_DEVICE(device),"cy-fw-image-single")) {
			self->fw_image_type = FW_IMAGE_TYPE_SINGLE;
		} else if (fu_device_has_custom_flag (FU_DEVICE(device),"cy-fw-image-dual-symmetric")) {
			self->fw_image_type = FW_IMAGE_TYPE_DUAL_SYMMETRIC;
		} else if (fu_device_has_custom_flag (FU_DEVICE(device),"cy-fw-image-dual-asymmetric")) {
			self->fw_image_type = FW_IMAGE_TYPE_DUAL_ASYMMETRIC;
		} else {
			g_set_error(error, FWUPD_ERROR,
				    FWUPD_ERROR_NOT_SUPPORTED,
				    "not supported fw image");
			return FALSE;
		}
		if (fu_device_has_custom_flag (FU_DEVICE(device),"cy-fw-primary-update-only"))
			self->fw_primary_update_only = TRUE;
	}
	return TRUE;
}

static gboolean
fu_ccgx_dock_bb_setup (FuDevice *device, GError **error)
{
	FuCcgxDockBb *self = FU_CCGX_DOCK_BB (device);
	PDFWAppVersion pd_device_fw_version;
	g_autofree gchar *device_str_version = NULL;
	g_autofree gchar *update_message = NULL;

	if (self->dm_device == DM_DEVICE_EXTERNAL_BB) { /* dm device type is external BB */
		g_debug ("Turn to MFG mode");

		if (!fu_ccgx_hid_enable_mfg_mode (device, self->usb_inf_num, error)) {
			g_prefix_error (error, "turn to mfg mode error:");
			return FALSE;
		}

		fu_device_set_status (device, FWUPD_STATUS_DEVICE_RESTART);
		fu_device_add_flag (FU_DEVICE(device), FWUPD_DEVICE_FLAG_WILL_DISAPPEAR);
		self->device_removed = TRUE;

	} else if (self->dm_device == DM_DEVICE_PD_I2C) { /* dm device type is pd i2c */

		/* configure device */
		if (!fu_ccgx_dock_bb_pd_i2c_configure(device,error)) {
			g_prefix_error (error, "i2c configure error:");
			return FALSE;
		}

		pd_device_fw_version.val = self->pd_device_data.current_version.val;

		if (self->fw_image_type == FW_IMAGE_TYPE_DUAL_ASYMMETRIC &&
		    self->pd_device_data.fw_mode ==  FW_MODE_FW1) {
			/* backup fw is running, set version to 0 to ensure update primary fw */
			pd_device_fw_version.val = 0;
		}
		g_debug ("===== Device Information =====");
		g_debug ("  Silicon ID : 0x%X", self->pd_device_data.silicon_id);
		g_debug ("  FW Mode :  FW%u", self->pd_device_data.fw_mode);

		if (self->fw_image_type == FW_IMAGE_TYPE_DUAL_SYMMETRIC) {
			g_debug ("  FW IMG :  DUAL SYMMETRIC");
		} else if (self->fw_image_type == FW_IMAGE_TYPE_DUAL_ASYMMETRIC) {
			g_debug ("  FW IMG :  DUAL ASYMMETRIC");
		}

		g_debug ("  Version : %u.%u.%u / 0x%02X(%c%c)", 
			(guint32)self->pd_device_data.current_version.ver.major,
			(guint32)self->pd_device_data.current_version.ver.minor,
			(guint32)self->pd_device_data.current_version.ver.build,
			self->pd_device_data.current_version.ver.type,
			((self->pd_device_data.current_version.ver.type>>8) & 0xff),
			((self->pd_device_data.current_version.ver.type) & 0xff));

		device_str_version = g_strdup_printf (
			"%u.%u.%u", 
			(guint32)pd_device_fw_version.ver.major,
			(guint32)pd_device_fw_version.ver.minor,
			(guint32)pd_device_fw_version.ver.build);

		g_debug ("Parsed version %s", device_str_version);

		/* update version of device */
		fu_device_set_version (FU_DEVICE (self), device_str_version);

		fu_device_add_flag (FU_DEVICE(device), FWUPD_DEVICE_FLAG_UPDATABLE);
		fu_device_add_flag (FU_DEVICE(device), FWUPD_DEVICE_FLAG_DUAL_IMAGE);
		fu_device_add_flag (FU_DEVICE(device), FWUPD_DEVICE_FLAG_CAN_VERIFY_IMAGE);

		if (self->fw_image_type == FW_IMAGE_TYPE_DUAL_SYMMETRIC) {
			update_message = g_strdup_printf ("%s", self->fw_update_message);
		} else if (self->fw_image_type == FW_IMAGE_TYPE_DUAL_ASYMMETRIC) {
			if (self->pd_device_data.fw_mode ==  FW_MODE_FW1) {
				/* update_message = g_strdup_printf("Primary %s %s", self->fw_update_message,self->fw_update_message_backup); */
				update_message = g_strdup_printf ("Primary %s", self->fw_update_message);
			} else if(self->pd_device_data.fw_mode ==  FW_MODE_FW2) {
				update_message = g_strdup_printf ("Backup %s %s", self->fw_update_message,self->fw_update_message_primary);
			} else {
				update_message = g_strdup_printf ("%s", self->fw_update_message);
			}
		} else {
			update_message = g_strdup_printf("%s", self->fw_update_message);
		}

		g_return_val_if_fail (update_message != NULL, FALSE);

		fwupd_device_set_update_message (FWUPD_DEVICE(device), update_message);
	}
	return TRUE;
}

static gboolean
fu_ccgx_dock_bb_usb_open (FuUsbDevice *device, GError **error)
{
	g_autoptr(GError) error_local = NULL;
	FuCcgxDockBb *self = FU_CCGX_DOCK_BB (device);
	GUsbDevice *usb_device = fu_usb_device_get_dev(device);

	if (self->claimed_interface)
		return TRUE;

	/* claim usb interface */
	if (!g_usb_device_claim_interface (usb_device,
					   self->usb_inf_num,
					   G_USB_DEVICE_CLAIM_INTERFACE_BIND_KERNEL_DRIVER,
					   &error_local)) {
		g_set_error (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_NOT_SUPPORTED,
			     "cannot claim interface %i: %s", 
			     self->usb_inf_num, error_local->message);
		return FALSE;
	}
	self->claimed_interface = TRUE;
	return TRUE;
}

static gboolean
fu_ccgx_dock_bb_usb_close (FuUsbDevice *device, GError **error)
{
	FuCcgxDockBb *self = FU_CCGX_DOCK_BB (device);

	if (!self->device_removed) {
		if (self->claimed_interface) {
			GUsbDevice *usb_device = fu_usb_device_get_dev (device);
			g_usb_device_release_interface (usb_device,
							self->usb_inf_num,
							G_USB_DEVICE_CLAIM_INTERFACE_BIND_KERNEL_DRIVER,
							NULL);
		}
	}
	self->claimed_interface = FALSE;
	return TRUE;
}

static void
fu_ccgx_dock_bb_finalize (GObject *object)
{
	FuCcgxDockBb *self = FU_CCGX_DOCK_BB (object);

	g_free (self->hpi_handle);
	g_free (self->fw_update_message);
	g_free (self->fw_update_message_primary);
	g_free (self->fw_update_message_backup);
	g_free (self->model_name);

	G_OBJECT_CLASS (fu_ccgx_dock_bb_parent_class)->finalize (object);
}

static void
fu_ccgx_dock_bb_init (FuCcgxDockBb *self)
{
	self->hpi_handle = g_new0 (CyHPIHandle, 1);
	fu_device_set_version_format (FU_DEVICE (self), FWUPD_VERSION_FORMAT_TRIPLET);
	fu_device_set_protocol (FU_DEVICE (self), "com.cypress.ccgx");
	fu_device_set_logical_id (FU_DEVICE(self), "dm");
}

static FuFirmware *
fu_ccgx_dock_bb_prepare_fw (FuDevice *device,
				       GBytes *fw,
				       FwupdInstallFlags flags,
				       GError **error)
{
	FuCcgxDockBb *self = FU_CCGX_DOCK_BB (device);
	g_autoptr(FuFirmware) firmware = fu_ccgx_cyacd_firmware_new ();

	fu_ccgx_cyacd_firmware_set_device_info (FU_CCGX_CYACD_FIRMWARE (firmware),
						self->fw_image_type,
						self->pd_device_data.silicon_id,
						self->pd_device_data.current_version.ver.type);
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

	fu_device_set_status (device, FWUPD_STATUS_DECOMPRESSING);
	if (!fu_firmware_parse (firmware, fw, flags, error))
		return NULL;

	g_debug ("fw prepare parsed version: %s", fu_firmware_get_version (firmware));
	return g_steal_pointer (&firmware);
}

static void
fu_ccgx_dock_bb_class_init (FuCcgxDockBbClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	FuDeviceClass *klass_device = FU_DEVICE_CLASS (klass);
	FuUsbDeviceClass *klass_usb_device = FU_USB_DEVICE_CLASS (klass);

	object_class->finalize		= fu_ccgx_dock_bb_finalize;
	klass_device->probe		= fu_ccgx_dock_bb_probe;
	klass_device->setup		= fu_ccgx_dock_bb_setup;
	klass_usb_device->open		= fu_ccgx_dock_bb_usb_open;
	klass_usb_device->close		= fu_ccgx_dock_bb_usb_close;
	klass_device->write_firmware	= fu_ccgx_dock_bb_write_fw;
	klass_device->prepare_firmware	= fu_ccgx_dock_bb_prepare_fw;
	klass_device->set_quirk_kv	= fu_ccgx_dock_bb_set_quirk_kv;
}

/**
 * fu_ccgx_dock_bb_reboot:
 * @device: #FuDevice
 * @error: a #GError or %NULL
 *
 * Reboot device
 *
 * Returns: %TRUE for success
*/
gboolean
fu_ccgx_dock_bb_reboot (FuDevice *device, GError **error)
{
	FuCcgxDockBb *self = FU_CCGX_DOCK_BB (device);
	g_autoptr(GError) error_local = NULL;
	gboolean need_to_enter_alt_mode = FALSE;
	CyHPIHandle*  hpi_handle = self->hpi_handle;

	g_return_val_if_fail (hpi_handle != NULL, FALSE);

	if (self->claimed_interface == FALSE) {
		g_set_error (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_NOT_SUPPORTED,
			     "reboot not supported due to no usb");
		g_warning ("no usb, reboot fail");
		return FALSE;
	}

	if (self->dm_device != DM_DEVICE_PD_I2C) {
		g_set_error (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_NOT_SUPPORTED,
			     "not reboot supported in this device");
		return FALSE;
	}

	if (self->fw_update_success == TRUE &&
	    self->fw_image_type == FW_IMAGE_TYPE_DUAL_ASYMMETRIC &&
	    self->pd_device_data.fw_mode == FW_MODE_FW2  &&
	    self->pd_device_data.fw_metadata[FW_MODE_FW1].metadata_valid == CCGX_METADATA_VALID_SIG &&
	    g_strcmp0 (self->model_name, CCGX_GEN2_DOCK_MODEL_NAME) == 0) {
	    need_to_enter_alt_mode = TRUE;
	}

	if (need_to_enter_alt_mode) {
		/* jump to Alt FW */
		g_debug ("Jump to Alt FW ... ");
		if (!fu_ccgx_hpi_cmd_jump_to_alt_fw(device, hpi_handle, error))
			return FALSE;
	} else {
		/* reset device */
		g_debug ("Reset Device ... ");
		if (!fu_ccgx_hpi_cmd_reset_device(device, hpi_handle, error))
			return FALSE;
	}

	self->device_removed = TRUE;
	fu_device_set_status (device, FWUPD_STATUS_DEVICE_RESTART);
	return TRUE;
}
