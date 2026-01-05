/*
 * Copyright 2018 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include <string.h>

#include "fu-wacom-usb-common.h"
#include "fu-wacom-usb-device.h"
#include "fu-wacom-usb-module.h"
#include "fu-wacom-usb-struct.h"

typedef struct {
	guint8 fw_type;
	guint8 command;
	guint8 status;
} FuWacomUsbModulePrivate;

G_DEFINE_TYPE_WITH_PRIVATE(FuWacomUsbModule, fu_wacom_usb_module, FU_TYPE_DEVICE)
#define GET_PRIVATE(o) (fu_wacom_usb_module_get_instance_private(o))

enum { PROP_0, PROP_FW_TYPE, PROP_LAST };

static void
fu_wacom_usb_module_to_string(FuDevice *device, guint idt, GString *str)
{
	FuWacomUsbModule *self = FU_WACOM_USB_MODULE(device);
	FuWacomUsbModulePrivate *priv = GET_PRIVATE(self);
	fwupd_codec_string_append(str,
				  idt,
				  "FwType",
				  fu_wacom_usb_module_fw_type_to_string(priv->fw_type));
	fwupd_codec_string_append(str,
				  idt,
				  "Status",
				  fu_wacom_usb_module_status_to_string(priv->status));
	fwupd_codec_string_append(str,
				  idt,
				  "Command",
				  fu_wacom_usb_module_command_to_string(priv->command));
}

static gboolean
fu_wacom_usb_module_refresh(FuWacomUsbModule *self, GError **error)
{
	FuWacomUsbDevice *parent;
	FuWacomUsbModulePrivate *priv = GET_PRIVATE(self);
	guint8 buf[] = {[0] = FU_WACOM_USB_REPORT_ID_MODULE,
			[1 ... FU_WACOM_USB_PACKET_LEN - 1] = 0xff};

	/* get from hardware */
	parent = FU_WACOM_USB_DEVICE(fu_device_get_parent(FU_DEVICE(self), error));
	if (parent == NULL)
		return FALSE;
	if (!fu_wacom_usb_device_get_feature_report(parent,
						    buf,
						    sizeof(buf),
						    FU_HID_DEVICE_FLAG_ALLOW_TRUNC,
						    error)) {
		g_prefix_error_literal(error, "failed to refresh status: ");
		fwupd_error_convert(error);
		return FALSE;
	}

	/* check fw type */
	if (priv->fw_type != buf[1]) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INTERNAL,
			    "Submodule GetFeature fw_Type invalid "
			    "got 0x%02x expected 0x%02x",
			    (guint)buf[1],
			    (guint)priv->fw_type);
		return FALSE;
	}

	/* current phase and status */
	if (priv->command != buf[2] || priv->status != buf[3]) {
		priv->command = buf[2];
		priv->status = buf[3];
		g_debug("command: %s, status: %s",
			fu_wacom_usb_module_command_to_string(priv->command),
			fu_wacom_usb_module_status_to_string(priv->status));
	}

	/* success */
	return TRUE;
}

static gboolean
fu_wacom_usb_module_refresh_cb(FuDevice *device, gpointer user_data, GError **error)
{
	FuWacomUsbModule *self = FU_WACOM_USB_MODULE(device);
	FuWacomUsbModulePrivate *priv = GET_PRIVATE(self);
	g_autoptr(GError) error_local = NULL;

	if (!fu_wacom_usb_module_refresh(self, &error_local)) {
		if (g_error_matches(error_local, FWUPD_ERROR, FWUPD_ERROR_NOT_FOUND))
			return TRUE;
		g_propagate_error(error, g_steal_pointer(&error_local));
		return FALSE;
	}

	/* retry not necessary for unrecoverable errors */
	if (priv->status != FU_WACOM_USB_MODULE_STATUS_BUSY)
		return TRUE;

	if (priv->status != FU_WACOM_USB_MODULE_STATUS_OK) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INTERNAL,
			    "refresh returned status 0x%x [%s]",
			    priv->status,
			    fu_wacom_usb_module_status_to_string(priv->status));
		return FALSE;
	}

	/* success */
	return TRUE;
}

gboolean
fu_wacom_usb_module_set_feature(FuWacomUsbModule *self,
				guint8 command,
				GBytes *blob, /* optional */
				FuProgress *progress,
				guint poll_interval, /* ms */
				guint busy_timeout,  /* ms */
				GError **error)
{
	FuWacomUsbDevice *parent;
	FuWacomUsbModulePrivate *priv = GET_PRIVATE(self);
	const guint8 *data;
	gsize len = 0;
	guint delay_ms;
	guint busy_poll_loops;
	guint8 buf[] = {
	    [0] = FU_WACOM_USB_REPORT_ID_MODULE,
	    [1] = priv->fw_type,
	    [2] = command,
	    [3 ... FU_WACOM_USB_PACKET_LEN - 1] = 0xff,
	};

	/* sanity check */
	g_return_val_if_fail(FU_IS_WACOM_USB_MODULE(self), FALSE);

	parent = FU_WACOM_USB_DEVICE(fu_device_get_parent(FU_DEVICE(self), error));
	if (parent == NULL)
		return FALSE;
	delay_ms =
	    fu_device_has_flag(FU_DEVICE(parent), FWUPD_DEVICE_FLAG_EMULATED) ? 10 : poll_interval;
	busy_poll_loops = busy_timeout / delay_ms;

	/* verify the size of the blob */
	if (blob != NULL) {
		data = g_bytes_get_data(blob, &len);
		if (!fu_memcpy_safe(buf,
				    sizeof(buf),
				    0x03, /* dst */
				    data,
				    len,
				    0x0, /* src */
				    len,
				    error)) {
			g_prefix_error_literal(error, "Submodule blob larger than buffer: ");
			return FALSE;
		}
	}

	/* tell the daemon the current status */
	switch (command) {
	case FU_WACOM_USB_MODULE_COMMAND_START:
		fu_progress_set_status(progress, FWUPD_STATUS_DEVICE_ERASE);
		break;
	case FU_WACOM_USB_MODULE_COMMAND_DATA:
		fu_progress_set_status(progress, FWUPD_STATUS_DEVICE_WRITE);
		break;
	case FU_WACOM_USB_MODULE_COMMAND_END:
		fu_progress_set_status(progress, FWUPD_STATUS_DEVICE_VERIFY);
		break;
	default:
		break;
	}

	/* send to hardware */
	if (!fu_wacom_usb_device_set_feature_report(parent,
						    buf,
						    sizeof(buf),
						    FU_HID_DEVICE_FLAG_ALLOW_TRUNC,
						    error)) {
		g_prefix_error_literal(error, "failed to set module feature: ");
		return FALSE;
	}

	/* wait for hardware */
	if (busy_poll_loops > 0) {
		fu_device_sleep(FU_DEVICE(self), delay_ms); /* settle before polling status */
		if (!fu_device_retry_full(FU_DEVICE(self),
					  fu_wacom_usb_module_refresh_cb,
					  busy_poll_loops,
					  delay_ms,
					  NULL,
					  error)) {
			g_prefix_error(error,
				       "failed to set feature %s: ",
				       fu_wacom_usb_module_command_to_string(command));
			return FALSE;
		}
		if (priv->status != FU_WACOM_USB_MODULE_STATUS_OK) {
			g_set_error(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INTERNAL,
				    "refresh returned status 0x%x [%s]",
				    priv->status,
				    fu_wacom_usb_module_status_to_string(priv->status));
			return FALSE;
		}
	}

	/* success */
	return TRUE;
}

static gboolean
fu_wacom_usb_module_cleanup(FuDevice *device,
			    FuProgress *progress,
			    FwupdInstallFlags flags,
			    GError **error)
{
	FuDevice *parent;
	g_autoptr(FuDeviceLocker) locker = NULL;

	/* sanity check */
	parent = fu_device_get_parent(device, error);
	if (parent == NULL)
		return FALSE;
	locker = fu_device_locker_new(parent, error);
	if (locker == NULL)
		return FALSE;
	return fu_device_cleanup(parent, progress, flags, error);
}

static gchar *
fu_wacom_usb_module_convert_version(FuDevice *device, guint64 version_raw)
{
	if (version_raw > G_MAXUINT16)
		return fu_version_from_uint32(version_raw, fu_device_get_version_format(device));

	return fu_version_from_uint16(version_raw, fu_device_get_version_format(device));
}

static void
fu_wacom_usb_module_get_property(GObject *object, guint prop_id, GValue *value, GParamSpec *pspec)
{
	FuWacomUsbModule *self = FU_WACOM_USB_MODULE(object);
	FuWacomUsbModulePrivate *priv = GET_PRIVATE(self);
	switch (prop_id) {
	case PROP_FW_TYPE:
		g_value_set_uint(value, priv->fw_type);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
		break;
	}
}

static void
fu_wacom_usb_module_set_property(GObject *object,
				 guint prop_id,
				 const GValue *value,
				 GParamSpec *pspec)
{
	FuWacomUsbModule *self = FU_WACOM_USB_MODULE(object);
	FuWacomUsbModulePrivate *priv = GET_PRIVATE(self);
	switch (prop_id) {
	case PROP_FW_TYPE:
		priv->fw_type = g_value_get_uint(value);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
		break;
	}
}

static void
fu_wacom_usb_module_init(FuWacomUsbModule *self)
{
	fu_device_add_protocol(FU_DEVICE(self), "com.wacom.usb");
	fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_UNSIGNED_PAYLOAD);
	fu_device_add_private_flag(FU_DEVICE(self), FU_DEVICE_PRIVATE_FLAG_MD_SET_FLAGS);
	fu_device_add_private_flag(FU_DEVICE(self), FU_DEVICE_PRIVATE_FLAG_PARENT_NAME_PREFIX);
	fu_device_set_version_format(FU_DEVICE(self), FWUPD_VERSION_FORMAT_BCD);
	fu_device_set_remove_delay(FU_DEVICE(self), FU_DEVICE_REMOVE_DELAY_RE_ENUMERATE);
	fu_device_set_proxy_gtype(FU_DEVICE(self), FU_TYPE_WACOM_USB_DEVICE);
}

static void
fu_wacom_usb_module_constructed(GObject *object)
{
	FuWacomUsbModule *self = FU_WACOM_USB_MODULE(object);
	FuWacomUsbModulePrivate *priv = GET_PRIVATE(self);
	FuDevice *proxy = fu_device_get_proxy(FU_DEVICE(self), NULL);

	/* not set in tests */
	if (proxy != NULL) {
		g_autofree gchar *devid = NULL;

		/* set vendor ID */
		fu_device_build_vendor_id_u16(FU_DEVICE(self),
					      "USB",
					      fu_device_get_vid(FU_DEVICE(proxy)));

		/* set USB physical and logical IDs */
		fu_device_incorporate(FU_DEVICE(self),
				      proxy,
				      FU_DEVICE_INCORPORATE_FLAG_PHYSICAL_ID);
		fu_device_set_logical_id(FU_DEVICE(self),
					 fu_wacom_usb_module_fw_type_to_string(priv->fw_type));

		/* append the firmware kind to the generated GUID */
		devid = g_strdup_printf("USB\\VID_%04X&PID_%04X-%s",
					fu_device_get_vid(proxy),
					fu_device_get_pid(proxy),
					fu_wacom_usb_module_fw_type_to_string(priv->fw_type));
		fu_device_add_instance_id(FU_DEVICE(self), devid);
	}

	G_OBJECT_CLASS(fu_wacom_usb_module_parent_class)->constructed(object);
}

static void
fu_wacom_usb_module_set_progress(FuDevice *device, FuProgress *progress)
{
	fu_progress_set_id(progress, G_STRLOC);
	fu_progress_add_step(progress, FWUPD_STATUS_DECOMPRESSING, 0, "prepare-fw");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_RESTART, 0, "detach");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_WRITE, 100, "write");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_RESTART, 0, "attach");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_BUSY, 0, "reload");
}

static void
fu_wacom_usb_module_class_init(FuWacomUsbModuleClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS(klass);
	GParamSpec *pspec;
	FuDeviceClass *device_class = FU_DEVICE_CLASS(klass);

	/* properties */
	object_class->get_property = fu_wacom_usb_module_get_property;
	object_class->set_property = fu_wacom_usb_module_set_property;

	/**
	 * FuWacomUsbModule:fw-type:
	 *
	 * The firmware kind.
	 */
	pspec = g_param_spec_uint("fw-type",
				  NULL,
				  NULL,
				  0,
				  G_MAXUINT,
				  0,
				  G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_NAME);
	g_object_class_install_property(object_class, PROP_FW_TYPE, pspec);

	object_class->constructed = fu_wacom_usb_module_constructed;
	device_class->to_string = fu_wacom_usb_module_to_string;
	device_class->cleanup = fu_wacom_usb_module_cleanup;
	device_class->set_progress = fu_wacom_usb_module_set_progress;
	device_class->convert_version = fu_wacom_usb_module_convert_version;
}
