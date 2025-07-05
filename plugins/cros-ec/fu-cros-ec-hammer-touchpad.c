#include "config.h"

#include <string.h>

#include "fu-cros-ec-common.h"
#include "fu-cros-ec-hammer-touchpad.h"
#include "fu-cros-ec-usb-device.h"

struct _FuCrosEcHammerTouchpad {
	FuDevice parent_instance;
	gchar *raw_version;
};

G_DEFINE_TYPE(FuCrosEcHammerTouchpad, fu_cros_ec_hammer_touchpad, FU_TYPE_DEVICE)

static gboolean
fu_cros_ec_hammer_touchpad_setup(FuDevice *device, GError **error)
{
	FuCrosEcHammerTouchpad *self = FU_CROS_EC_HAMMER_TOUCHPAD(device);
	guint32 error_code;
	fu_device_set_version(FU_DEVICE(device), "1.1.1");
	return TRUE;
}

static void
fu_cros_ec_hammer_touchpad_finalize(GObject *object)
{
	FuCrosEcHammerTouchpad *self = FU_CROS_EC_HAMMER_TOUCHPAD(object);
	g_free(self->raw_version);
	G_OBJECT_CLASS(fu_cros_ec_hammer_touchpad_parent_class)->finalize(object);
}

static void
fu_cros_ec_hammer_touchpad_init(FuCrosEcHammerTouchpad *self)
{
	fu_device_add_protocol(FU_DEVICE(self), "com.google.usb.crosec");
	fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_UPDATABLE);
	fu_device_add_private_flag(FU_DEVICE(self), FU_DEVICE_PRIVATE_FLAG_REPLUG_MATCH_GUID);
	fu_device_add_private_flag(FU_DEVICE(self), FU_DEVICE_PRIVATE_FLAG_DETACH_PREPARE_FIRMWARE);
	fu_device_set_version_format(FU_DEVICE(self), FWUPD_VERSION_FORMAT_TRIPLET);
	fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_SIGNED_PAYLOAD);
	// fu_device_set_firmware_gtype(FU_DEVICE(self), FU_TYPE_CROS_EC_FIRMWARE);
}

static void
fu_cros_ec_hammer_touchpad_class_init(FuCrosEcHammerTouchpadClass *klass)
{
	FuDeviceClass *device_class = FU_DEVICE_CLASS(klass);
	GObjectClass *object_class = G_OBJECT_CLASS(klass);
	object_class->finalize = fu_cros_ec_hammer_touchpad_finalize;
	// device_class->prepare_firmware = fu_cros_ec_usb_device_prepare_firmware;
	device_class->setup = fu_cros_ec_hammer_touchpad_setup;
	// device_class->to_string = fu_cros_ec_usb_device_to_string;
	// device_class->write_firmware = fu_cros_ec_usb_device_write_firmware;
	// device_class->probe = fu_cros_ec_usb_device_probe;
	// device_class->set_progress = fu_cros_ec_usb_device_set_progress;
	// device_class->reload = fu_cros_ec_usb_device_reload;
	// device_class->replace = fu_cros_ec_usb_device_replace;
	// device_class->cleanup = fu_cros_ec_usb_device_cleanup;
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
