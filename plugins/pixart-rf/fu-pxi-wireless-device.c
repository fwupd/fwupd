/*
 * Copyright (C) 2020 Jimmy Yu <Jimmy_yu@pixart.com>
 * Copyright (C) 2020 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */
#include "config.h"

#ifdef HAVE_HIDRAW_H
#include <linux/hidraw.h>
#include <linux/input.h>
#endif

#include "fu-firmware.h"
#include "fu-common.h"
#include "fu-chunk.h"
#include "fu-pxi-wireless-device.h"
#include "fu-pxi-wireless-module.h"
#include "fu-pxi-dongle-module.h"
#include "fu-pxi-firmware.h"
#include "fu-pxi-device.h"
#include "fu-pxi-common.h"

struct _FuPxiWirelessDevice {
	FuUdevDevice	 	parent_instance;
	guint8		 	status;
	guint8		 	new_flow;
	guint16		 	offset;
	guint16		 	checksum;
	guint32		 	max_object_size;
	guint16		 	mtu_size;
	guint16		 	prn_threshold;
	guint8		 	spec_check_result;
	guint8			sn;
	guint			vendor;
	guint			product;
};

G_DEFINE_TYPE (FuPxiWirelessDevice, fu_pxi_wireless_device, FU_TYPE_UDEV_DEVICE)

#ifdef HAVE_HIDRAW_H
static gboolean
fu_pxi_wireless_device_get_raw_info (FuPxiWirelessDevice *self, struct hidraw_devinfo *info, GError **error)
{
	if (!fu_udev_device_ioctl (FU_UDEV_DEVICE (self),
				   HIDIOCGRAWINFO, (guint8 *) info,
				   NULL, error)) {
		return FALSE;
	}
	return TRUE;
}
#endif

static gboolean
fu_pxi_wireless_device_set_feature (FuPxiWirelessDevice *self, const guint8 *buf, guint bufsz, GError **error)
{
#ifdef HAVE_HIDRAW_H
	if (g_getenv ("FWUPD_PIXART_RF_VERBOSE") != NULL) {
		fu_common_dump_raw (G_LOG_DOMAIN, "SetFeature", buf, bufsz);
	}
	return fu_udev_device_ioctl (FU_UDEV_DEVICE (self),
				     HIDIOCSFEATURE(bufsz), (guint8 *) buf,
				     NULL, error);
#else
	g_set_error_literal (error,
			     G_IO_ERROR,
			     G_IO_ERROR_NOT_SUPPORTED,
			     "<linux/hidraw.h> not available");
	return FALSE;
#endif
}

static gboolean
fu_pxi_wireless_device_get_feature (FuPxiWirelessDevice *self, guint8 *buf, guint bufsz, GError **error)
{
#ifdef HAVE_HIDRAW_H
	if (!fu_udev_device_ioctl (FU_UDEV_DEVICE (self),
				   HIDIOCGFEATURE(bufsz), buf,
				   NULL, error)) {
		return FALSE;
	}
	if (g_getenv ("FWUPD_PIXART_RF_VERBOSE") != NULL) {
		fu_common_dump_raw (G_LOG_DOMAIN, "GetFeature", buf, bufsz);
	}
	return TRUE;
#else
	g_set_error_literal (error,
			     G_IO_ERROR,
			     G_IO_ERROR_NOT_SUPPORTED,
			     "<linux/hidraw.h> not available");
	return FALSE;
#endif
}

static void
fu_pxi_wireless_device_to_string (FuDevice *device, guint idt, GString *str)
{
	FuPxiWirelessDevice *self = FU_PXI_WIRELESS_DEVICE (device);
	fu_common_string_append_kx (str, idt, "Status", self->status);
	fu_common_string_append_kx (str, idt, "NewFlow", self->new_flow);
	fu_common_string_append_kx (str, idt, "CurrentObjectOffset", self->offset);
	fu_common_string_append_kx (str, idt, "CurrentChecksum", self->checksum);
	fu_common_string_append_kx (str, idt, "MaxObjectSize", self->max_object_size);
	fu_common_string_append_kx (str, idt, "MtuSize", self->mtu_size);
	fu_common_string_append_kx (str, idt, "PacketReceiptNotificationThreshold", self->prn_threshold);
	fu_common_string_append_kx (str, idt, "Vendor", self->vendor);
	fu_common_string_append_kx (str, idt, "Product", self->product);
}

static gboolean
fu_pxi_wireless_device_fw_ota_init_new (FuPxiWirelessDevice *device, gsize bufsz, GError **error)
{
	guint8 fw_version[10] = { 0x0 };
	g_autoptr(GByteArray) wireless_module_cmd = g_byte_array_new ();
	g_autoptr(GByteArray) ota_cmd = g_byte_array_new ();
	FuPxiWirelessDevice *self = FU_PXI_WIRELESS_DEVICE (device);

	fu_byte_array_append_uint8 (ota_cmd, 0X06);					/* ota init new command length */
	fu_byte_array_append_uint8 (ota_cmd, FU_PXI_DEVICE_CMD_FW_OTA_INIT_NEW);	/* ota ota init new op code */
	fu_byte_array_append_uint32 (ota_cmd, bufsz, G_LITTLE_ENDIAN);			/* fw size */
	fu_byte_array_append_uint8 (ota_cmd, 0x0);					/* ota setting */
	g_byte_array_append (ota_cmd, fw_version, sizeof(fw_version));			/* ota version */

	self->sn++;
	if (!fu_pxi_common_composite_module_cmd (FU_PXI_DEVICE_CMD_FW_OTA_INIT_NEW,
							self->sn,
							FU_PXI_WIRELESS_MODULE_TARGET_DONGLE,
							wireless_module_cmd,
							ota_cmd, error))
		return FALSE;

	if (!fu_pxi_wireless_device_set_feature (self, wireless_module_cmd->data, wireless_module_cmd->len, error))
		return FALSE;

	return TRUE;
}

static gboolean
fu_pxi_wireless_device_fw_ota_ini_new_check (FuPxiWirelessDevice *device, GError **error)
{

	g_autoptr(GByteArray) wireless_module_cmd = g_byte_array_new ();
	g_autoptr(GByteArray) ota_cmd = g_byte_array_new ();
	guint8 res[FU_PXI_WIRELESS_MODULE_OTA_BUF_SZ] = { 0x0 };
	FuPxiWirelessDevice *self = FU_PXI_WIRELESS_DEVICE (device);

	/* ota command */
	fu_byte_array_append_uint8 (ota_cmd, 0x1);					/* ota command length */
	fu_byte_array_append_uint8 (ota_cmd, FU_PXI_DEVICE_CMD_FW_OTA_INIT_NEW_CHECK);	/* ota command */
	self->sn++;

	/* get pixart wireless module ota command */
	if (!fu_pxi_common_composite_module_cmd (FU_PXI_DEVICE_CMD_FW_OTA_INIT_NEW_CHECK,
							self->sn,
							FU_PXI_WIRELESS_MODULE_TARGET_DONGLE,
							wireless_module_cmd,
							ota_cmd, error))
		return FALSE;

	if (!fu_pxi_wireless_device_set_feature (self, wireless_module_cmd->data,
							wireless_module_cmd->len, error))
		return FALSE;


	/* delay for wireless module device read command */
	g_usleep (5 * 1000);

	memset (res, 0, sizeof(res));
	res[0] = PXI_HID_WIRELESS_DEV_OTA_REPORT_ID;
	if (!fu_pxi_wireless_device_get_feature (self, res, 32, error))
		return FALSE;

	/* shared state */
	if (!fu_common_read_uint8_safe (res, sizeof(res), 0x3 + 0x6,
					&self->status, error))
		return FALSE;
	if (!fu_common_read_uint8_safe (res, sizeof(res), 0x4 + 0x6,
					&self->new_flow, error))
		return FALSE;
	if (!fu_common_read_uint16_safe (res, sizeof(res), 0x5 + 0x6,
					 &self->offset, G_LITTLE_ENDIAN, error))
		return FALSE;
	if (!fu_common_read_uint16_safe (res, sizeof(res), 0x7 + 0x6,
					 &self->checksum, G_LITTLE_ENDIAN, error))
		return FALSE;
	if (!fu_common_read_uint32_safe (res, sizeof(res), 0x9 + 0x6,
					 &self->max_object_size, G_LITTLE_ENDIAN, error))
		return FALSE;
	if (!fu_common_read_uint16_safe (res, sizeof(res), 0xd + 0x6,
					 &self->mtu_size, G_LITTLE_ENDIAN, error))
		return FALSE;
	if (!fu_common_read_uint16_safe (res, sizeof(res), 0xf + 0x6,
					 &self->prn_threshold, G_LITTLE_ENDIAN, error))
		return FALSE;
	if (!fu_common_read_uint8_safe (res, sizeof(res), 0x11 + 0x6,
					&self->spec_check_result, error))
		return FALSE;


	return TRUE;
}

static gboolean
fu_pxi_wireless_device_get_module_info (FuPxiWirelessDevice *device, struct ota_fw_dev_model *model, GError **error)
{
	g_autoptr(GByteArray) ota_cmd = g_byte_array_new ();
	g_autoptr(GByteArray) wireless_module_cmd = g_byte_array_new ();
	guint8 res[FU_PXI_WIRELESS_MODULE_OTA_BUF_SZ] = { 0x0 };
	guint16 checksum = 0;
	g_autofree gchar *version_str = NULL;
	wireless_module_cmd = g_byte_array_new ();

	fu_byte_array_append_uint8 (ota_cmd, 0x1);						/* ota init new command length */
	fu_byte_array_append_uint8 (ota_cmd, FU_PXI_DEVICE_CMD_FW_OTA_GET_MODEL);
	device->sn++;

	if (!fu_pxi_common_composite_module_cmd (FU_PXI_DEVICE_CMD_FW_OTA_GET_MODEL,
				device->sn,
				0x0,
				wireless_module_cmd,
				ota_cmd, error))
		return FALSE;
	if (!fu_pxi_wireless_device_set_feature (device, wireless_module_cmd->data,
							wireless_module_cmd->len, error))
		return FALSE;

	g_usleep (5 * 1000);
	memset (res, 0, sizeof(res));
	res[0] = PXI_HID_WIRELESS_DEV_OTA_REPORT_ID;

	if (!fu_pxi_wireless_device_get_feature (device, res, sizeof(res), error))
		return FALSE;

	fu_common_dump_raw (G_LOG_DOMAIN, "model_info", res, 96);

	if (!fu_common_read_uint8_safe (res, sizeof(res), 0x9,
					&model->status, error))
		return FALSE;

	if (!fu_memcpy_safe (model->name, FU_PXI_DEVICE_MODEL_NAME_LEN, 0x0,	/* dst */
			     res ,FU_PXI_WIRELESS_MODULE_OTA_BUF_SZ, 0xa,				/* src */
			     FU_PXI_DEVICE_MODEL_NAME_LEN, error))
		return FALSE;

	if (!fu_common_read_uint8_safe (res, sizeof(res), 0x16,
					&model->type, error))
		return FALSE;
	if (!fu_common_read_uint8_safe (res, sizeof(res), 0x17,
					&model->target, error))
		return FALSE;

	if (!fu_memcpy_safe (model->version, 5, 0x0,	/* dst */
			     res ,sizeof(res), 0x18,		/* src */
			     5, error))
		return FALSE;

	if (!fu_common_read_uint16_safe (res, sizeof(res), 0x1D,
					 &checksum, G_LITTLE_ENDIAN, error))

		return FALSE;

	/* set current version and model name */
	version_str = g_strndup ((gchar *) model->version, 5);
	model->checksum = checksum;

	if (g_getenv ("FWUPD_PIXART_RF_VERBOSE") != NULL) {
		g_debug ("checksum %x",model->checksum);
		g_debug ("version_str %s",version_str);
	}

	return TRUE;
}

static gboolean
fu_pxi_wireless_device_get_module_num (FuPxiWirelessDevice *device, guint8 *num_of_models, GError **error)
{
	g_autoptr(GByteArray) ota_cmd = g_byte_array_new ();
	g_autoptr(GByteArray) wireless_module_cmd = g_byte_array_new ();
	guint8 res[FU_PXI_WIRELESS_MODULE_OTA_BUF_SZ] = { 0x0 };
	FuPxiWirelessDevice *self = FU_PXI_WIRELESS_DEVICE (device);
	wireless_module_cmd = g_byte_array_new ();


	fu_byte_array_append_uint8 (ota_cmd, 0x1);						/* ota init new command length */
	fu_byte_array_append_uint8 (ota_cmd, FU_PXI_DEVICE_CMD_FW_OTA_GET_NUM_OF_MODELS);	/* ota ota init new op code */

	self->sn++;
	if (!fu_pxi_common_composite_module_cmd (FU_PXI_DEVICE_CMD_FW_OTA_GET_NUM_OF_MODELS,
					self->sn,
					0x0,
					wireless_module_cmd,
					ota_cmd, error))
		return FALSE;
	if (!fu_pxi_wireless_device_set_feature (self, wireless_module_cmd->data,
							wireless_module_cmd->len, error))
		return FALSE;

	g_usleep (5 * 1000);

	memset (res, 0, sizeof(res));
	res[0] = PXI_HID_WIRELESS_DEV_OTA_REPORT_ID;

	if (!fu_pxi_wireless_device_get_feature (device, res, sizeof(res), error))
		return FALSE;

	if (g_getenv ("FWUPD_PIXART_RF_VERBOSE") != NULL) {
		fu_common_dump_raw (G_LOG_DOMAIN, "res from get model num",
				    res, sizeof(res));
	}
	if (!fu_common_read_uint8_safe (res, sizeof(res), 0xa,
					num_of_models, error))
		return FALSE;

	return TRUE;
}

static gboolean
fu_pxi_wireless_device_add_modules (FuPxiWirelessDevice *device, GError **error)
{
#ifdef HAVE_HIDRAW_H
	g_autofree gchar *child_id = NULL;
	FuPxiWirelessModule *wireless_module = NULL;
	FuPxiDongleModule *dongle_module = NULL;
	struct ota_fw_dev_model model = {0x0};

	/* get the all wireless modules info */
	if (!fu_pxi_wireless_device_get_module_info (device, &model, error))
		return FALSE;
	child_id = g_strdup_printf ("HIDRAW\\VEN_%04X&DEV_%04X&MODEL_%s",
				 device->vendor,
				 device->product,
				 model.name);

	if (model.type == Dongle) {

		dongle_module = fu_pxi_dongle_module_new (&model);
		fu_device_set_logical_id (FU_DEVICE (dongle_module), child_id);
		fu_device_add_guid (FU_DEVICE (dongle_module), child_id);
		fu_device_set_name (FU_DEVICE (dongle_module), g_strndup ((gchar *) model.name, FU_PXI_DEVICE_MODEL_NAME_LEN));
		fu_device_set_version (FU_DEVICE (dongle_module), g_strndup ((gchar *) model.version, 5));
		fu_device_add_child (FU_DEVICE (device), FU_DEVICE (dongle_module));

	} else {

		wireless_module = fu_pxi_wireless_module_new (&model);
		fu_device_set_logical_id (FU_DEVICE (wireless_module), child_id);
		fu_device_add_guid (FU_DEVICE (wireless_module), child_id);
		fu_device_set_name (FU_DEVICE (wireless_module), g_strndup ((gchar *) model.name, FU_PXI_DEVICE_MODEL_NAME_LEN));
		fu_device_set_version (FU_DEVICE (wireless_module), g_strndup ((gchar *) model.version, 5));
		fu_device_add_child (FU_DEVICE (device), FU_DEVICE (wireless_module));
	}
	return TRUE;
#else
	g_set_error_literal (error,
			     G_IO_ERROR,
			     G_IO_ERROR_NOT_SUPPORTED,
			     "<linux/hidraw.h> not available");
	return FALSE;
#endif
}

static gboolean
fu_pxi_wireless_device_setup_guid (FuPxiWirelessDevice *device, GError **error)
{
#ifdef HAVE_HIDRAW_H
	struct hidraw_devinfo hid_raw_info = { 0x0 };
	g_autofree gchar *devid = NULL;
	g_autoptr(GString) dev_name = NULL;

	/* extra GUID with device name */
	if (!fu_pxi_wireless_device_get_raw_info (device, &hid_raw_info ,error))
		return FALSE;

	device->vendor = (guint) hid_raw_info.vendor;
	device->product = (guint) hid_raw_info.product;

	dev_name = g_string_new (fu_device_get_name (FU_DEVICE (device)));
	g_string_ascii_up (dev_name);
	fu_common_string_replace (dev_name, " ", "_");
	devid = g_strdup_printf ("HIDRAW\\VEN_%04X&DEV_%04X&NAME_%s",
				 (guint) hid_raw_info.vendor,
				 (guint) hid_raw_info.product,
				 dev_name->str);
	fu_device_add_instance_id (FU_DEVICE (device), devid);
	return TRUE;
#else
	g_set_error_literal (error,
			     G_IO_ERROR,
			     G_IO_ERROR_NOT_SUPPORTED,
			     "<linux/hidraw.h> not available");
	return FALSE;
#endif
}

static gboolean
fu_pxi_wireless_device_check_modules (FuPxiWirelessDevice *device, GError **error)
{
	guint8 num = 0;

	/* get the num of wireless modules */
	if(!fu_pxi_wireless_device_get_module_num (device, &num , error))
		return FALSE;

	if (g_getenv ("FWUPD_PIXART_RF_VERBOSE") != NULL) {
		g_debug ("num %u",num);
	}
	/* add wireless modules */
	for (guint8 idx = 0; idx < num; idx++) {
		if (!fu_pxi_wireless_device_add_modules (device, error))
			return FALSE;
	}
	return TRUE;
}

static gboolean
fu_pxi_wireless_device_setup (FuDevice *device, GError **error)
{
	FuPxiWirelessDevice *self = FU_PXI_WIRELESS_DEVICE(device);

	if (!fu_pxi_wireless_device_setup_guid (self ,error)) {
		g_prefix_error (error, "failed to setup GUID: ");
		return FALSE;
	}
	if (!fu_pxi_wireless_device_fw_ota_init_new (self, 0x0000, error)) {
		g_prefix_error (error, "failed to OTA init new: ");
		return FALSE;
	}
	if (!fu_pxi_wireless_device_fw_ota_ini_new_check (self ,error)) {
		g_prefix_error (error, "failed to OTA init new check: ");
		return FALSE;
	}
	if (!fu_pxi_wireless_device_check_modules (self ,error)) {
		g_prefix_error (error, "failed to add wireless module: ");
		return FALSE;
	}

	return TRUE;
}

static gboolean
fu_pxi_wireless_device_probe (FuDevice *device, GError **error)
{
	/* set the logical and physical ID */
	if (!fu_udev_device_set_logical_id (FU_UDEV_DEVICE (device), "hid", error))
		return FALSE;
	return fu_udev_device_set_physical_id (FU_UDEV_DEVICE (device), "hid", error);
}

static void
fu_pxi_wireless_device_init (FuPxiWirelessDevice *self)
{
	g_debug ("fu_pxi_wireless_device_init");
	fu_device_add_flag (FU_DEVICE (self), FWUPD_DEVICE_FLAG_UPDATABLE);
	fu_device_set_version_format (FU_DEVICE (self), FWUPD_VERSION_FORMAT_TRIPLET);
	fu_device_add_vendor_id (FU_DEVICE (self), "USB:0x093A");
	fu_device_add_protocol (FU_DEVICE (self), "com.pixart.rf");
}

static void
fu_pxi_wireless_device_class_init (FuPxiWirelessDeviceClass *klass)
{
	FuDeviceClass *klass_device = FU_DEVICE_CLASS (klass);
	klass_device->to_string = fu_pxi_wireless_device_to_string;
	klass_device->setup = fu_pxi_wireless_device_setup;
	klass_device->probe = fu_pxi_wireless_device_probe;
}
