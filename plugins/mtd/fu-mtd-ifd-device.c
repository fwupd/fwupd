/*
 * Copyright 2024 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include "fu-ifd-struct.h"
#include "fu-mtd-ifd-device.h"

struct _FuMtdIfdDevice {
	FuDevice parent_instance;
	FuIfdImage *img;
};

G_DEFINE_TYPE(FuMtdIfdDevice, fu_mtd_ifd_device, FU_TYPE_DEVICE)

static void
fu_mtd_ifd_device_add_security_attr_desc(FuMtdIfdDevice *self, FuSecurityAttrs *attrs)
{
	g_autoptr(FwupdSecurityAttr) attr = NULL;
	FuIfdAccess ifd_access_global = FALSE;
	FuIfdRegion regions[] = {FU_IFD_REGION_BIOS, FU_IFD_REGION_ME, FU_IFD_REGION_EC};

	/* create attr */
	attr = fu_device_security_attr_new(FU_DEVICE(self), FWUPD_SECURITY_ATTR_ID_SPI_DESCRIPTOR);
	fwupd_security_attr_set_result_success(attr, FWUPD_SECURITY_ATTR_RESULT_LOCKED);
	fu_security_attrs_append(attrs, attr);

	/* check each */
	for (guint i = 0; i < G_N_ELEMENTS(regions); i++) {
		FuIfdAccess ifd_access = fu_ifd_image_get_access(self->img, regions[i]);
		g_autofree gchar *ifd_accessstr = fu_ifd_access_to_string(ifd_access);
		fwupd_security_attr_add_metadata(attr,
						 fu_ifd_region_to_string(regions[i]),
						 ifd_accessstr);
		ifd_access_global |= ifd_access;
	}
	if (ifd_access_global & FU_IFD_ACCESS_WRITE) {
		fwupd_security_attr_add_flag(attr, FWUPD_SECURITY_ATTR_FLAG_ACTION_CONTACT_OEM);
		fwupd_security_attr_set_result(attr, FWUPD_SECURITY_ATTR_RESULT_NOT_VALID);
		return;
	}

	/* success */
	fwupd_security_attr_add_flag(attr, FWUPD_SECURITY_ATTR_FLAG_SUCCESS);
}

static void
fu_mtd_ifd_device_add_security_attrs(FuDevice *device, FuSecurityAttrs *attrs)
{
	FuMtdIfdDevice *self = FU_MTD_IFD_DEVICE(device);

	if (self->img == NULL)
		return;
	if (fu_firmware_get_idx(FU_FIRMWARE(self->img)) == FU_IFD_REGION_DESC)
		fu_mtd_ifd_device_add_security_attr_desc(self, attrs);
}

static const gchar *
fu_mtd_ifd_device_region_to_name(FuIfdRegion region)
{
	if (region == FU_IFD_REGION_DESC)
		return "IFD descriptor region";
	if (region == FU_IFD_REGION_BIOS)
		return "BIOS";
	if (region == FU_IFD_REGION_ME)
		return "Intel Management Engine";
	if (region == FU_IFD_REGION_GBE)
		return "Gigabit Ethernet";
	if (region == FU_IFD_REGION_PLATFORM)
		return "Platform firmware";
	if (region == FU_IFD_REGION_DEVEXP)
		return "Device Firmware";
	if (region == FU_IFD_REGION_BIOS2)
		return "BIOS Backup";
	if (region == FU_IFD_REGION_EC)
		return "Embedded Controller";
	if (region == FU_IFD_REGION_IE)
		return "Innovation Engine";
	if (region == FU_IFD_REGION_10GBE)
		return "10 Gigabit Ethernet";
	return NULL;
}

static gboolean
fu_mtd_ifd_device_probe(FuDevice *device, GError **error)
{
	FuMtdIfdDevice *self = FU_MTD_IFD_DEVICE(device);

	if (self->img != NULL) {
		FuIfdRegion region = fu_firmware_get_idx(FU_FIRMWARE(self->img));
		fu_device_set_name(device, fu_mtd_ifd_device_region_to_name(region));
		fu_device_set_logical_id(device, fu_ifd_region_to_string(region));
		fu_device_add_instance_str(device, "REGION", fu_ifd_region_to_string(region));
	}
	if (!fu_device_build_instance_id(device, error, "IFD", "REGION", NULL))
		return FALSE;

	/* success */
	return TRUE;
}

static void
fu_mtd_ifd_device_init(FuMtdIfdDevice *self)
{
	fu_device_add_icon(FU_DEVICE(self), FU_DEVICE_ICON_COMPUTER);
	fu_device_add_private_flag(FU_DEVICE(self), FU_DEVICE_PRIVATE_FLAG_PARENT_NAME_PREFIX);
}

static void
fu_mtd_ifd_device_class_init(FuMtdIfdDeviceClass *klass)
{
	FuDeviceClass *device_class = FU_DEVICE_CLASS(klass);
	device_class->probe = fu_mtd_ifd_device_probe;
	device_class->add_security_attrs = fu_mtd_ifd_device_add_security_attrs;
}

FuMtdIfdDevice *
fu_mtd_ifd_device_new(FuDevice *parent, FuIfdImage *img)
{
	FuMtdIfdDevice *self =
	    g_object_new(FU_TYPE_MTD_IFD_DEVICE, "parent", parent, "proxy", parent, NULL);
	self->img = g_object_ref(img);
	return self;
}
