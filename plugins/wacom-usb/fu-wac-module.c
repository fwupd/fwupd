/*
 * Copyright (C) 2018 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include <string.h>

#include "fu-wac-module.h"
#include "fu-wac-common.h"
#include "fu-wac-device.h"

#define FU_WAC_MODULE_STATUS_OK				0
#define FU_WAC_MODULE_STATUS_BUSY			1
#define FU_WAC_MODULE_STATUS_ERR_CRC			2
#define FU_WAC_MODULE_STATUS_ERR_CMD			3
#define FU_WAC_MODULE_STATUS_ERR_HW_ACCESS_FAIL		4
#define FU_WAC_MODULE_STATUS_ERR_FLASH_NO_SUPPORT	5
#define FU_WAC_MODULE_STATUS_ERR_MODE_WRONG		6
#define FU_WAC_MODULE_STATUS_ERR_MPU_NO_SUPPORT		7
#define FU_WAC_MODULE_STATUS_ERR_VERSION_NO_SUPPORT	8
#define FU_WAC_MODULE_STATUS_ERR_ERASE			9
#define FU_WAC_MODULE_STATUS_ERR_WRITE			10
#define FU_WAC_MODULE_STATUS_ERR_EXIT			11
#define FU_WAC_MODULE_STATUS_ERR			12
#define FU_WAC_MODULE_STATUS_ERR_INVALID_OP		13
#define FU_WAC_MODULE_STATUS_ERR_WRONG_IMAGE		14

typedef struct {
	GUsbDevice		*usb_device;
	guint8			 fw_type;
	guint8			 command;
	guint8			 status;
} FuWacModulePrivate;

G_DEFINE_TYPE_WITH_PRIVATE (FuWacModule, fu_wac_module, FU_TYPE_DEVICE)
#define GET_PRIVATE(o) (fu_wac_module_get_instance_private (o))

enum {
	PROP_0,
	PROP_FW_TYPE,
	PROP_USB_DEVICE,
	PROP_LAST
};

static const gchar *
fu_wac_module_fw_type_to_string (guint8 fw_type)
{
	if (fw_type == FU_WAC_MODULE_FW_TYPE_TOUCH)
		return "touch";
	if (fw_type == FU_WAC_MODULE_FW_TYPE_BLUETOOTH)
		return "bluetooth";
	if (fw_type == FU_WAC_MODULE_FW_TYPE_EMR_CORRECTION)
		return "emr-correction";
	if (fw_type == FU_WAC_MODULE_FW_TYPE_BLUETOOTH_HID)
		return "bluetooth-hid";
	return NULL;
}

static const gchar *
fu_wac_module_command_to_string (guint8 command)
{
	if (command == FU_WAC_MODULE_COMMAND_START)
		return "start";
	if (command == FU_WAC_MODULE_COMMAND_DATA)
		return "data";
	if (command == FU_WAC_MODULE_COMMAND_END)
		return "end";
	return NULL;
}

static const gchar *
fu_wac_module_status_to_string (guint8 status)
{
	if (status == FU_WAC_MODULE_STATUS_OK)
		return "ok";
	if (status == FU_WAC_MODULE_STATUS_BUSY)
		return "busy";
	if (status == FU_WAC_MODULE_STATUS_ERR_CRC)
		return "err-crc";
	if (status == FU_WAC_MODULE_STATUS_ERR_CMD)
		return "err-cmd";
	if (status == FU_WAC_MODULE_STATUS_ERR_HW_ACCESS_FAIL)
		return "err-hw-access-fail";
	if (status == FU_WAC_MODULE_STATUS_ERR_FLASH_NO_SUPPORT)
		return "err-flash-no-support";
	if (status == FU_WAC_MODULE_STATUS_ERR_MODE_WRONG)
		return "err-mode-wrong";
	if (status == FU_WAC_MODULE_STATUS_ERR_MPU_NO_SUPPORT)
		return "err-mpu-no-support";
	if (status == FU_WAC_MODULE_STATUS_ERR_VERSION_NO_SUPPORT)
		return "erro-version-no-support";
	if (status == FU_WAC_MODULE_STATUS_ERR_ERASE)
		return "err-erase";
	if (status == FU_WAC_MODULE_STATUS_ERR_WRITE)
		return "err-write";
	if (status == FU_WAC_MODULE_STATUS_ERR_EXIT)
		return "err-exit";
	if (status == FU_WAC_MODULE_STATUS_ERR)
		return "err-err";
	if (status == FU_WAC_MODULE_STATUS_ERR_INVALID_OP)
		return "err-invalid-op";
	if (status == FU_WAC_MODULE_STATUS_ERR_WRONG_IMAGE)
		return "err-wrong-image";
	return NULL;
}

static void
fu_wac_module_to_string (FuDevice *device, guint idt, GString *str)
{
	FuWacModule *self = FU_WAC_MODULE (device);
	FuWacModulePrivate *priv = GET_PRIVATE (self);
	fu_common_string_append_kv (str, idt, "FwType",
				    fu_wac_module_fw_type_to_string (priv->fw_type));
	fu_common_string_append_kv (str, idt, "Status",
				    fu_wac_module_status_to_string (priv->status));
	fu_common_string_append_kv (str, idt, "Command",
				    fu_wac_module_command_to_string (priv->command));
}

static gboolean
fu_wac_module_refresh (FuWacModule *self, GError **error)
{
	FuWacDevice *parent_device = FU_WAC_DEVICE (fu_device_get_parent (FU_DEVICE (self)));
	FuWacModulePrivate *priv = GET_PRIVATE (self);
	guint8 buf[] = { [0] = FU_WAC_REPORT_ID_MODULE,
			 [1 ... FU_WAC_PACKET_LEN - 1] = 0xff };

	/* get from hardware */
	if (!fu_wac_device_get_feature_report (parent_device, buf, sizeof(buf),
					       FU_WAC_DEVICE_FEATURE_FLAG_ALLOW_TRUNC |
					       FU_WAC_DEVICE_FEATURE_FLAG_NO_DEBUG,
					       error)) {
		g_prefix_error (error, "failed to refresh status: ");
		return FALSE;
	}

	/* check fw type */
	if (priv->fw_type != buf[1]) {
		g_set_error (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_INTERNAL,
			     "Submodule GetFeature fw_Type invalid "
			     "got 0x%02x expected 0x%02x",
			     (guint) buf[1], (guint) priv->fw_type);
		return FALSE;
	}

	/* current phase and status */
	if (priv->command != buf[2] || priv->status != buf[3]) {
		priv->command = buf[2];
		priv->status = buf[3];
		g_debug ("command: %s, status: %s",
			 fu_wac_module_command_to_string (priv->command),
			 fu_wac_module_status_to_string (priv->status));
	}

	/* success */
	return TRUE;
}

gboolean
fu_wac_module_set_feature (FuWacModule *self,
			   guint8 command,
			   GBytes *blob, /* optional */
			   GError **error)
{
	FuWacDevice *parent_device = FU_WAC_DEVICE (fu_device_get_parent (FU_DEVICE (self)));
	FuWacModulePrivate *priv = GET_PRIVATE (self);
	const guint8 *data;
	gsize len = 0;
	guint busy_poll_loops = 100; /* 1s */
	guint8 buf[] = { [0] = FU_WAC_REPORT_ID_MODULE,
			 [1] = priv->fw_type,
			 [2] = command,
			 [3 ... FU_WAC_PACKET_LEN - 1] = 0xff };

	/* verify the size of the blob */
	if (blob != NULL) {
		data = g_bytes_get_data (blob, &len);
		if (len > 509) {
			g_set_error (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_INTERNAL,
				     "Submodule SetFeature blob larger than "
				     "buffer %" G_GSIZE_FORMAT, len);
			return FALSE;
		}
	}

	/* build packet */
	if (len > 0)
		memcpy (&buf[3], data, len);

	/* tell the daemon the current status */
	switch (command) {
	case FU_WAC_MODULE_COMMAND_START:
		fu_device_set_status (FU_DEVICE (self), FWUPD_STATUS_DEVICE_ERASE);
		break;
	case FU_WAC_MODULE_COMMAND_DATA:
		fu_device_set_status (FU_DEVICE (self), FWUPD_STATUS_DEVICE_WRITE);
		break;
	case FU_WAC_MODULE_COMMAND_END:
		fu_device_set_status (FU_DEVICE (self), FWUPD_STATUS_DEVICE_VERIFY);
		break;
	default:
		break;
	}

	/* send to hardware */
	if (!fu_wac_device_set_feature_report (parent_device, buf, len + 3,
					       FU_WAC_DEVICE_FEATURE_FLAG_ALLOW_TRUNC,
					       error)) {
		g_prefix_error (error, "failed to set module feature: ");
		return FALSE;
	}

	/* special case StartProgram, as it can take much longer as it is
	 * erasing the blocks (15s) */
	if (command == FU_WAC_MODULE_COMMAND_START)
		busy_poll_loops *= 15;

	/* wait for hardware */
	for (guint i = 0; i < busy_poll_loops; i++) {
		if (!fu_wac_module_refresh (self, error))
			return FALSE;
		if (priv->status == FU_WAC_MODULE_STATUS_BUSY) {
			g_usleep (10000); /* 10ms */
			continue;
		}
		if (priv->status == FU_WAC_MODULE_STATUS_OK)
			break;
		g_set_error (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_INTERNAL,
			     "Failed to SetFeature: %s",
			     fu_wac_module_status_to_string (priv->status));
		return FALSE;
	}

	/* too many retries */
	if (priv->status != FU_WAC_MODULE_STATUS_OK) {
		g_set_error (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_INTERNAL,
			     "Timed out after %u loops with status %s",
			     busy_poll_loops,
			     fu_wac_module_status_to_string (priv->status));
		return FALSE;
	}

	/* success */
	return TRUE;
}

static void
fu_wac_module_get_property (GObject *object, guint prop_id,
			    GValue *value, GParamSpec *pspec)
{
	FuWacModule *self = FU_WAC_MODULE (object);
	FuWacModulePrivate *priv = GET_PRIVATE (self);
	switch (prop_id) {
	case PROP_FW_TYPE:
		g_value_set_uint (value, priv->fw_type);
		break;
	case PROP_USB_DEVICE:
		g_value_set_object (value, priv->usb_device);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

static void
fu_wac_module_set_property (GObject *object, guint prop_id,
			    const GValue *value, GParamSpec *pspec)
{
	FuWacModule *self = FU_WAC_MODULE (object);
	FuWacModulePrivate *priv = GET_PRIVATE (self);
	switch (prop_id) {
	case PROP_FW_TYPE:
		priv->fw_type = g_value_get_uint (value);
		break;
	case PROP_USB_DEVICE:
		g_set_object (&priv->usb_device, g_value_get_object (value));
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

static void
fu_wac_module_init (FuWacModule *self)
{
}

static void
fu_wac_module_constructed (GObject *object)
{
	FuWacModule *self = FU_WAC_MODULE (object);
	FuWacModulePrivate *priv = GET_PRIVATE (self);
	g_autofree gchar *devid = NULL;
	g_autofree gchar *vendor_id = NULL;

	/* set vendor ID */
	vendor_id = g_strdup_printf ("USB:0x%04X", g_usb_device_get_vid (priv->usb_device));
	fu_device_set_vendor_id (FU_DEVICE (self), vendor_id);

	/* set USB physical and logical IDs */
	fu_device_set_physical_id (FU_DEVICE (self),
				   g_usb_device_get_platform_id (priv->usb_device));
	fu_device_set_logical_id (FU_DEVICE (self),
				  fu_wac_module_fw_type_to_string (priv->fw_type));

	/* append the firmware kind to the generated GUID */
	devid = g_strdup_printf ("USB\\VID_%04X&PID_%04X-%s",
				 g_usb_device_get_vid (priv->usb_device),
				 g_usb_device_get_pid (priv->usb_device),
				 fu_wac_module_fw_type_to_string (priv->fw_type));
	fu_device_add_instance_id (FU_DEVICE (self), devid);

	G_OBJECT_CLASS (fu_wac_module_parent_class)->constructed (object);
}

static void
fu_wac_module_finalize (GObject *object)
{
	FuWacModule *self = FU_WAC_MODULE (object);
	FuWacModulePrivate *priv = GET_PRIVATE (self);
	if (priv->usb_device != NULL)
		g_object_unref (priv->usb_device);
	G_OBJECT_CLASS (fu_wac_module_parent_class)->finalize (object);
}

static void
fu_wac_module_class_init (FuWacModuleClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	GParamSpec *pspec;
	FuDeviceClass *klass_device = FU_DEVICE_CLASS (klass);

	/* properties */
	object_class->get_property = fu_wac_module_get_property;
	object_class->set_property = fu_wac_module_set_property;
	pspec = g_param_spec_object ("usb-device", NULL, NULL,
				     G_USB_TYPE_DEVICE,
				     G_PARAM_READWRITE |
				     G_PARAM_CONSTRUCT |
				     G_PARAM_STATIC_NAME);
	g_object_class_install_property (object_class, PROP_USB_DEVICE, pspec);
	pspec = g_param_spec_uint ("fw-type", NULL, NULL,
				   0, G_MAXUINT, 0,
				   G_PARAM_READWRITE |
				   G_PARAM_CONSTRUCT_ONLY |
				   G_PARAM_STATIC_NAME);
	g_object_class_install_property (object_class, PROP_FW_TYPE, pspec);

	object_class->constructed = fu_wac_module_constructed;
	object_class->finalize = fu_wac_module_finalize;
	klass_device->to_string = fu_wac_module_to_string;
}
