/*
 * Copyright (C) 2015 Richard Hughes <richard@hughsie.com>
 * Copyright (C) 2016 Mario Limonciello <mario.limonciello@dell.com>
 * Copyright (C) 2021 Jeffrey Lin <jlin@kinet-ic.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include <fcntl.h>

#include "fu-kinetic-mst-common.h"
#include "fu-kinetic-mst-device.h"

struct _FuKineticMstDevice {
    FuUdevDevice        parent_instance;
    gchar *             system_type;
    FuKineticMstFamily  family;
    FuKineticMstMode    mode;
};

G_DEFINE_TYPE (FuKineticMstDevice, fu_kinetic_mst_device, FU_TYPE_UDEV_DEVICE)

static void
fu_kinetic_mst_device_finalize (GObject *object)
{
	FuKineticMstDevice *self = FU_KINETIC_MST_DEVICE (object);

	g_free (self->system_type);

	G_OBJECT_CLASS (fu_kinetic_mst_device_parent_class)->finalize (object);
}

static void
fu_kinetic_mst_device_init (FuKineticMstDevice *self)
{
	fu_device_set_protocol (FU_DEVICE (self), "com.kinetic.mst");
	fu_device_set_vendor (FU_DEVICE (self), "Kinetic");
	fu_device_add_vendor_id (FU_DEVICE (self), "DRM_DP_AUX_DEV:0x06CB");    // <TODO> How to determine the vendor ID?
	fu_device_set_summary (FU_DEVICE (self), "Multi-Stream Transport Device");
	fu_device_add_icon (FU_DEVICE (self), "video-display");
	fu_device_set_version_format (FU_DEVICE (self), FWUPD_VERSION_FORMAT_TRIPLET);  // <TODO> What's Kinetic's version format?
	fu_udev_device_set_flags (FU_UDEV_DEVICE (self),
                              FU_UDEV_DEVICE_FLAG_OPEN_READ |
                              FU_UDEV_DEVICE_FLAG_OPEN_WRITE |
                              FU_UDEV_DEVICE_FLAG_VENDOR_FROM_PARENT);
}

FuKineticMstDevice *
fu_kinetic_mst_device_new (FuUdevDevice *device)
{
	FuKineticMstDevice *self = g_object_new (FU_TYPE_KINETIC_MST_DEVICE, NULL);
	fu_device_incorporate (FU_DEVICE (self), FU_DEVICE (device));
	return self;
}

void
fu_kinetic_mst_device_set_system_type (FuKineticMstDevice *self, const gchar *system_type)
{
	g_return_if_fail (FU_IS_KINETIC_MST_DEVICE (self));
	self->system_type = g_strdup (system_type);
}

static void
fu_kinetic_mst_device_class_init (FuKineticMstDeviceClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	FuDeviceClass *klass_device = FU_DEVICE_CLASS (klass);
	//object_class->finalize = fu_kinetic_mst_device_finalize;
	//klass_device->to_string = fu_kinetic_mst_device_to_string;
	//klass_device->rescan = fu_kinetic_mst_device_rescan;
	//klass_device->write_firmware = fu_kinetic_mst_device_write_firmware;
	//klass_device->prepare_firmware = fu_kinetic_mst_device_prepare_firmware;
	//klass_device->probe = fu_kinetic_mst_device_probe;
}

