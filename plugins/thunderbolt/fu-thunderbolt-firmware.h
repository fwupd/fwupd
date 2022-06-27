/*
 * Copyright (C) 2017 Intel Corporation.
 * Copyright (C) 2020 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#pragma once

#include <fwupdplugin.h>

#define FU_TYPE_THUNDERBOLT_FIRMWARE (fu_thunderbolt_firmware_get_type())
G_DECLARE_DERIVABLE_TYPE(FuThunderboltFirmware,
			 fu_thunderbolt_firmware,
			 FU,
			 THUNDERBOLT_FIRMWARE,
			 FuFirmware)

typedef enum {
	_SECTION_DIGITAL,
	_SECTION_DROM,
	_SECTION_ARC_PARAMS,
	_SECTION_DRAM_UCODE,
	_SECTION_LAST
} FuThunderboltSection;

typedef enum {
	_FAMILY_UNKNOWN,
	_FAMILY_FR,
	_FAMILY_WR,
	_FAMILY_AR,
	_FAMILY_AR_C,
	_FAMILY_TR,
	_FAMILY_BB,
	_FAMILY_MR,
} FuThunderboltFamily;

struct _FuThunderboltFirmwareClass {
	FuFirmwareClass parent_class;
	gboolean (*parse)(FuFirmware *self,
			  GBytes *fw,
			  gsize offset,
			  FwupdInstallFlags flags,
			  GError **error);
	/*< private >*/
	gpointer padding[28];
};

/* byte offsets in firmware image */
#define FU_TBT_OFFSET_NATIVE 0x7B
#define FU_TBT_CHUNK_SZ	     0x40

FuThunderboltFirmware *
fu_thunderbolt_firmware_new(void);
gboolean
fu_thunderbolt_firmware_is_host(FuThunderboltFirmware *self);
gboolean
fu_thunderbolt_firmware_is_native(FuThunderboltFirmware *self);
gboolean
fu_thunderbolt_firmware_get_has_pd(FuThunderboltFirmware *self);
guint16
fu_thunderbolt_firmware_get_device_id(FuThunderboltFirmware *self);
guint16
fu_thunderbolt_firmware_get_vendor_id(FuThunderboltFirmware *self);
guint16
fu_thunderbolt_firmware_get_model_id(FuThunderboltFirmware *self);
guint8
fu_thunderbolt_firmware_get_flash_size(FuThunderboltFirmware *self);
void
fu_thunderbolt_firmware_set_digital(FuThunderboltFirmware *self, guint32 offset);
gboolean
fu_thunderbolt_firmware_read_location(FuThunderboltFirmware *self,
				      FuThunderboltSection section,
				      guint32 offset,
				      guint8 *buf,
				      guint32 len,
				      GError **error);
