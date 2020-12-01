/*
 * Copyright (C) 2020 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include "fu-synaptics-rmi-ps2-device.h"

struct _FuSynapticsRmiPs2Device {
	FuUdevDevice		 parent_instance;
};

G_DEFINE_TYPE (FuSynapticsRmiPs2Device, fu_synaptics_rmi_ps2_device, FU_TYPE_UDEV_DEVICE)

static void
fu_synaptics_rmi_ps2_device_to_string (FuDevice *device, guint idt, GString *str)
{
//	FuSynapticsRmiPs2Device *self = FU_SYNAPTICS_RMI_PS2_DEVICE (device);
//	fu_common_string_append_kx (str, idt, "I2cAddr", self->i2c_addr);
}

static gboolean
fu_synaptics_rmi_ps2_device_probe (FuUdevDevice *device, GError **error)
{
	/* psmouse is the usual mode, but serio is needed for update */
	if (g_strcmp0 (fu_udev_device_get_driver (device), "serio_raw") == 0) {
		fu_device_add_flag (FU_DEVICE (device),
				    FWUPD_DEVICE_FLAG_IS_BOOTLOADER);
	} else {
		fu_device_remove_flag (FU_DEVICE (device),
				       FWUPD_DEVICE_FLAG_IS_BOOTLOADER);
	}

	/* set the physical ID */
	return fu_udev_device_set_physical_id (device, "platform", error);
}

static gboolean
fu_synaptics_rmi_ps2_device_setup (FuDevice *device, GError **error)
{
//	FuSynapticsRmiPs2Device *self = FU_SYNAPTICS_RMI_PS2_DEVICE (device);
	return TRUE;
}

static gboolean
fu_synaptics_rmi_ps2_device_open (FuUdevDevice *device, GError **error)
{
//	FuSynapticsRmiPs2Device *self = FU_SYNAPTICS_RMI_PS2_DEVICE (device);
	return TRUE;
}

static FuFirmware *
fu_synaptics_rmi_ps2_device_prepare_firmware (FuDevice *device,
					      GBytes *fw,
					      FwupdInstallFlags flags,
					      GError **error)
{
//	FuSynapticsRmiPs2Device *self = FU_SYNAPTICS_RMI_PS2_DEVICE (device);
	g_autoptr(FuFirmware) firmware = fu_firmware_new ();

	if (!fu_firmware_parse (firmware, fw, flags, error))
		return NULL;

	/* success */
	return g_steal_pointer (&firmware);
}

static gboolean
fu_synaptics_rmi_ps2_device_write_firmware (FuDevice *device,
					    FuFirmware *firmware,
					    FwupdInstallFlags flags,
					    GError **error)
{
//	FuSynapticsRmiPs2Device *self = FU_SYNAPTICS_RMI_PS2_DEVICE (device);
//DRAW THE REST OF THE OWL
	return TRUE;
}

static gboolean
fu_synaptics_rmi_ps2_device_detach (FuDevice *device, GError **error)
{
	/* sanity check */
	if (fu_device_has_flag (device, FWUPD_DEVICE_FLAG_IS_BOOTLOADER)) {
		g_debug ("already in bootloader mode, skipping");
		return TRUE;
	}

	/* put in serio_raw mode so that we can do register writes */
	if (!fu_udev_device_write_sysfs (FU_UDEV_DEVICE (device),
					 "drvctl", "serio_raw", error)) {
		g_prefix_error (error, "failed to write to drvctl: ");
		return FALSE;
	}

	/* rescan device */
	return fu_device_probe (device, error);
}

static gboolean
fu_synaptics_rmi_ps2_device_attach (FuDevice *device, GError **error)
{
	/* sanity check */
	if (!fu_device_has_flag (device, FWUPD_DEVICE_FLAG_IS_BOOTLOADER)) {
		g_debug ("already in runtime mode, skipping");
		return TRUE;
	}

	/* back to psmouse */
	if (!fu_udev_device_write_sysfs (FU_UDEV_DEVICE (device),
					 "drvctl", "psmouse", error)) {
		g_prefix_error (error, "failed to write to drvctl: ");
		return FALSE;
	}

	/* rescan device */
	return fu_device_probe (device, error);
}

static void
fu_synaptics_rmi_ps2_device_init (FuSynapticsRmiPs2Device *self)
{
	fu_device_add_flag (FU_DEVICE (self), FWUPD_DEVICE_FLAG_INTERNAL);
	fu_device_add_flag (FU_DEVICE (self), FWUPD_DEVICE_FLAG_UPDATABLE);
	fu_device_set_protocol (FU_DEVICE (self), "com.synaptics.rmi");
	fu_device_set_name (FU_DEVICE (self), "TouchStyk");
	fu_device_set_vendor (FU_DEVICE (self), "Synaptics");
	fu_device_set_vendor_id (FU_DEVICE (self), "HIDRAW:0x06CB");
	fu_device_set_version_format (FU_DEVICE (self), FWUPD_VERSION_FORMAT_HEX); //FIXME?
}

static void
fu_synaptics_rmi_ps2_device_finalize (GObject *object)
{
	G_OBJECT_CLASS (fu_synaptics_rmi_ps2_device_parent_class)->finalize (object);
}

static void
fu_synaptics_rmi_ps2_device_class_init (FuSynapticsRmiPs2DeviceClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	FuDeviceClass *klass_device = FU_DEVICE_CLASS (klass);
	FuUdevDeviceClass *klass_udev_device = FU_UDEV_DEVICE_CLASS (klass);
	object_class->finalize = fu_synaptics_rmi_ps2_device_finalize;
	klass_device->to_string = fu_synaptics_rmi_ps2_device_to_string;
	klass_device->attach = fu_synaptics_rmi_ps2_device_attach;
	klass_device->detach = fu_synaptics_rmi_ps2_device_detach;
	klass_device->setup = fu_synaptics_rmi_ps2_device_setup;
	klass_device->reload = fu_synaptics_rmi_ps2_device_setup;
	klass_device->write_firmware = fu_synaptics_rmi_ps2_device_write_firmware;
	klass_device->prepare_firmware = fu_synaptics_rmi_ps2_device_prepare_firmware;
	klass_udev_device->probe = fu_synaptics_rmi_ps2_device_probe;
	klass_udev_device->open = fu_synaptics_rmi_ps2_device_open;
}
