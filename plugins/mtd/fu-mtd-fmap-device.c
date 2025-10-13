/*
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include "fu-firmware.h"
#include "fu-mtd-fmap-device.h"

struct _FuMtdFmapDevice {
	FuDevice parent_instance;
	FuFirmware *img;
};

G_DEFINE_TYPE(FuMtdFmapDevice, fu_mtd_fmap_device, FU_TYPE_DEVICE)

static gboolean
fu_mtd_fmap_device_probe(FuDevice *device, GError **error)
{
	FuMtdFmapDevice *self = FU_MTD_FMAP_DEVICE(device);
	const gchar *region_id = fu_firmware_get_id(self->img);

	if (region_id != NULL) {
		fu_device_set_name(device, region_id);
		fu_device_set_logical_id(device, region_id);
		fu_device_add_instance_str(device, "REGION", region_id);
	}
	if (fu_firmware_get_version(self->img) != NULL)
		fu_device_set_version(device, fu_firmware_get_version(self->img));
	if (fu_firmware_get_version_raw(self->img) != G_MAXUINT64)
		fu_device_set_version_raw(device, fu_firmware_get_version_raw(self->img));
	if (fu_firmware_get_size(self->img) > 0)
		fu_device_set_firmware_size(device, fu_firmware_get_size(self->img));
	if (!fu_device_build_instance_id(device, error, "FMAP", "REGION", NULL))
		return FALSE;

	return TRUE;
}

static void
fu_mtd_fmap_device_finalize(GObject *object)
{
	FuMtdFmapDevice *self = FU_MTD_FMAP_DEVICE(object);

	g_clear_object(&self->img);

	G_OBJECT_CLASS(fu_mtd_fmap_device_parent_class)->finalize(object);
}

static void
fu_mtd_fmap_device_init(FuMtdFmapDevice *self)
{
	fu_device_add_icon(FU_DEVICE(self), FU_DEVICE_ICON_COMPUTER);
	fu_device_add_private_flag(FU_DEVICE(self),
					FU_DEVICE_PRIVATE_FLAG_PARENT_NAME_PREFIX);
}

static void
fu_mtd_fmap_device_class_init(FuMtdFmapDeviceClass *klass)
{
	FuDeviceClass *device_class = FU_DEVICE_CLASS(klass);
	GObjectClass *object_class = G_OBJECT_CLASS(klass);

	device_class->probe = fu_mtd_fmap_device_probe;
	object_class->finalize = fu_mtd_fmap_device_finalize;
}

FuMtdFmapDevice *
fu_mtd_fmap_device_new(FuDevice *parent, FuFirmware *img)
{
	FuMtdFmapDevice *self =
	    g_object_new(FU_TYPE_MTD_FMAP_DEVICE, "parent", parent, "proxy", parent, NULL);
	self->img = g_object_ref(img);
	return self;
}
