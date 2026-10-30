/*
 * Copyright 2026 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include "fu-common.h"
#include "fu-coswid-collection.h"
#include "fu-coswid-firmware.h"

struct _FuCoswidCollection {
	FuLinearFirmware parent_instance;
};

G_DEFINE_TYPE(FuCoswidCollection, fu_coswid_collection, FU_TYPE_LINEAR_FIRMWARE)

static void
fu_coswid_collection_init(FuCoswidCollection *self)
{
	fu_firmware_add_flag(FU_FIRMWARE(self), FU_FIRMWARE_FLAG_NO_AUTO_DETECTION);
}

static void
fu_coswid_collection_constructed(GObject *object)
{
	G_OBJECT_CLASS(fu_coswid_collection_parent_class)->constructed(object);
	fu_linear_firmware_set_image_gtype(FU_LINEAR_FIRMWARE(object), FU_TYPE_COSWID_FIRMWARE);
}

static void
fu_coswid_collection_class_init(FuCoswidCollectionClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS(klass);
	FuFirmwareClass *firmware_class = FU_FIRMWARE_CLASS(klass);
	object_class->constructed = fu_coswid_collection_constructed;
	fu_firmware_set_size_max(firmware_class, 32 * FU_MB);
}

/**
 * fu_coswid_collection_new:
 *
 * Creates a new #FuFirmware collection made up of concatenated CoSWID images.
 *
 * Since: 2.2.1
 **/
FuFirmware *
fu_coswid_collection_new(void)
{
	return g_object_new(FU_TYPE_COSWID_COLLECTION, NULL);
}
