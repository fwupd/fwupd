/*
 * Copyright 2026 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include "fu-common.h"
#include "fu-ifwi-cpd-image.h"

struct _FuIfwiCpdImage {
	FuFirmware parent_instance;
};

G_DEFINE_TYPE(FuIfwiCpdImage, fu_ifwi_cpd_image, FU_TYPE_FIRMWARE)

static void
fu_ifwi_cpd_image_init(FuIfwiCpdImage *self)
{
}

static void
fu_ifwi_cpd_image_class_init(FuIfwiCpdImageClass *klass)
{
	FuFirmwareClass *firmware_class = FU_FIRMWARE_CLASS(klass);
	fu_firmware_add_image_gtype(firmware_class, FU_TYPE_FIRMWARE);
	fu_firmware_set_size_max(firmware_class, 128 * FU_MB);
}

FuFirmware *
fu_ifwi_cpd_image_new(void)
{
	return FU_FIRMWARE(g_object_new(FU_TYPE_IFWI_CPD_IMAGE, NULL));
}
