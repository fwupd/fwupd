/*
 * Copyright 2025 Hamed Elgizery
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include <string.h>

#include "fu-cros-ec-common.h"
#include "fu-cros-ec-hammer-touchpad.h"
#include "fu-cros-ec-struct.h"
#include "fu-cros-ec-usb-device.h"

#define ST_VENDOR_ID   0x0483
#define ELAN_VENDOR_ID 0x04f3

struct _FuCrosEcHammerTouchpad {
	FuDevice parent_instance;
	guint16 vendor;
	guint32 fw_address;
	guint32 fw_size;
	gchar *allowed_fw_hash;
	guint16 id;
	guint16 fw_version;
	guint16 fw_checksum;
};

G_DEFINE_TYPE(FuCrosEcHammerTouchpad, fu_cros_ec_hammer_touchpad, FU_TYPE_DEVICE)

static gboolean
fu_cros_ec_hammer_touchpad_set_version(FuCrosEcHammerTouchpad *self, GError **error)
{
	gchar *base_fw_ver;
	switch (self->vendor) {
	case ST_VENDOR_ID:
		base_fw_ver = g_strdup_printf("%d.%d",
					      self->fw_version & 0x00ff,
					      (self->fw_version & 0xff00) >> 8); // TODO: Fix
		break;
	case ELAN_VENDOR_ID:
		base_fw_ver = g_strdup_printf("%d.0", self->fw_version);
		break;
	default:
		return FALSE;
	}
	fu_device_set_version(FU_DEVICE(self), base_fw_ver);

	/* success */
	return TRUE;
}

static gboolean
fu_cros_ec_hammer_touchpad_get_info(FuCrosEcHammerTouchpad *self, GError **error)
{
	FuDevice *parent = fu_device_get_parent(FU_DEVICE(self));
	guint32 error_code;
	guint16 subcommand = FU_CROS_EC_UPDATE_EXTRA_CMD_TOUCHPAD_INFO;
	guint8 command_body[2] = {0x0}; /* max command body size */
	gsize command_body_size = 0;
	g_autoptr(FuStructCrosEcTouchpadGetInfoResponsePdu) tpi_rpdu =
	    fu_struct_cros_ec_touchpad_get_info_response_pdu_new();
	guint8 *response = tpi_rpdu->data;
	gsize response_size = tpi_rpdu->len;
	g_autoptr(GError) error_local = NULL;

	g_return_val_if_fail(FU_IS_CROS_EC_USB_DEVICE(parent), FALSE);

	if (!fu_cros_ec_usb_device_send_subcommand(FU_CROS_EC_USB_DEVICE(parent),
						   subcommand,
						   command_body,
						   command_body_size,
						   response,
						   &response_size,
						   FALSE,
						   &error_local))
		return FALSE;

	error_code = fu_struct_cros_ec_touchpad_get_info_response_pdu_get_status(tpi_rpdu);
	if (error_code != 0) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INTERNAL,
			    "target touchpad reporting error %u",
			    error_code);
		return FALSE;
	}

	self->vendor = fu_struct_cros_ec_touchpad_get_info_response_pdu_get_vendor(tpi_rpdu);
	self->fw_address =
	    fu_struct_cros_ec_touchpad_get_info_response_pdu_get_fw_address(tpi_rpdu);
	self->fw_size = fu_struct_cros_ec_touchpad_get_info_response_pdu_get_fw_size(tpi_rpdu);
	self->allowed_fw_hash =
	    fu_struct_cros_ec_touchpad_get_info_response_pdu_get_allowed_fw_hash(tpi_rpdu);
	self->id = fu_struct_cros_ec_touchpad_get_info_response_pdu_get_id(tpi_rpdu);
	self->fw_version =
	    fu_struct_cros_ec_touchpad_get_info_response_pdu_get_fw_version(tpi_rpdu);
	self->fw_checksum =
	    fu_struct_cros_ec_touchpad_get_info_response_pdu_get_fw_checksum(tpi_rpdu);
	fu_cros_ec_hammer_touchpad_set_version(self, error);

	/* success */
	return TRUE;
}

static gboolean
fu_cros_ec_hammer_touchpad_setup(FuDevice *device, GError **error)
{
	FuCrosEcHammerTouchpad *self = FU_CROS_EC_HAMMER_TOUCHPAD(device);

	if (!fu_cros_ec_hammer_touchpad_get_info(self, error))
		return FALSE;

	/* success */
	return TRUE;
}

static void
fu_cros_ec_hammer_touchpad_finalize(GObject *object)
{
	FuCrosEcHammerTouchpad *self = FU_CROS_EC_HAMMER_TOUCHPAD(object);
	g_free(self->allowed_fw_hash);
	G_OBJECT_CLASS(fu_cros_ec_hammer_touchpad_parent_class)->finalize(object);
}

static void
fu_cros_ec_hammer_touchpad_init(FuCrosEcHammerTouchpad *self)
{
	fu_device_add_protocol(FU_DEVICE(self), "com.google.usb.crosec");
	fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_UPDATABLE);
	fu_device_add_private_flag(FU_DEVICE(self), FU_DEVICE_PRIVATE_FLAG_REPLUG_MATCH_GUID);
	fu_device_add_private_flag(FU_DEVICE(self), FU_DEVICE_PRIVATE_FLAG_DETACH_PREPARE_FIRMWARE);
	fu_device_set_version_format(FU_DEVICE(self), FWUPD_VERSION_FORMAT_PAIR);
	fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_SIGNED_PAYLOAD);
}

static void
fu_cros_ec_hammer_touchpad_class_init(FuCrosEcHammerTouchpadClass *klass)
{
	FuDeviceClass *device_class = FU_DEVICE_CLASS(klass);
	GObjectClass *object_class = G_OBJECT_CLASS(klass);
	object_class->finalize = fu_cros_ec_hammer_touchpad_finalize;
	device_class->setup = fu_cros_ec_hammer_touchpad_setup;
}

FuCrosEcHammerTouchpad *
fu_cros_ec_hammer_touchpad_new(FuDevice *parent)
{
	FuCrosEcHammerTouchpad *self = NULL;
	FuContext *ctx = fu_device_get_context(parent);
	g_autofree gchar *instance_id = NULL;

	self = g_object_new(FU_TYPE_CROS_EC_HAMMER_TOUCHPAD, "context", ctx, NULL);
	fu_device_incorporate(FU_DEVICE(self), parent, FU_DEVICE_INCORPORATE_FLAG_PHYSICAL_ID);
	fu_device_set_name(FU_DEVICE(self), "Hammer Touchpad");
	fu_device_set_logical_id(FU_DEVICE(self), "cros-ec-hammer-touchpad");
	instance_id = g_strdup_printf("USB\\VID_%04X&PID_%04X&TOUCHPAD",
				      fu_device_get_vid(FU_DEVICE(parent)),
				      fu_device_get_pid(FU_DEVICE(parent)));
	fu_device_add_instance_id(FU_DEVICE(self), instance_id);
	return self;
}
