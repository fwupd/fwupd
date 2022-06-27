/*
 * Copyright (C) 2020 Mario Limonciello <mario.limonciello@dell.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include <fwupdplugin.h>

#include "fu-thunderbolt-firmware-update.h"
#include "fu-thunderbolt-firmware.h"

struct _FuThunderboltFirmwareUpdate {
	FuThunderboltFirmwareClass parent_instance;
};

G_DEFINE_TYPE(FuThunderboltFirmwareUpdate,
	      fu_thunderbolt_firmware_update,
	      FU_TYPE_THUNDERBOLT_FIRMWARE)

static inline gboolean
fu_thunderbolt_firmware_valid_farb_pointer(guint32 pointer)
{
	return pointer != 0 && pointer != 0xFFFFFF;
}

static gboolean
fu_thunderbolt_firmware_read_farb_pointer_impl(FuThunderboltFirmwareUpdate *self,
					       FuThunderboltSection section,
					       guint32 offset,
					       guint32 *value,
					       GError **error)
{
	FuThunderboltFirmware *tbt = FU_THUNDERBOLT_FIRMWARE(self);
	guint32 tmp = 0;
	if (!fu_thunderbolt_firmware_read_location(tbt,
						   section,
						   offset,
						   (guint8 *)&tmp,
						   3, /* 24 bits */
						   error)) {
		g_prefix_error(error, "failed to read farb pointer: ");
		return FALSE;
	}
	*value = GUINT32_FROM_LE(tmp);
	return TRUE;
}

/* returns invalid FARB pointer on error */
static guint32
fu_thunderbolt_firmware_read_farb_pointer(FuThunderboltFirmwareUpdate *self, GError **error)
{
	guint32 value;
	if (!fu_thunderbolt_firmware_read_farb_pointer_impl(self,
							    _SECTION_DIGITAL,
							    0x0,
							    &value,
							    error))
		return 0;
	if (fu_thunderbolt_firmware_valid_farb_pointer(value))
		return value;

	if (!fu_thunderbolt_firmware_read_farb_pointer_impl(self,
							    _SECTION_DIGITAL,
							    0x1000,
							    &value,
							    error))
		return 0;
	if (!fu_thunderbolt_firmware_valid_farb_pointer(value)) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INVALID_FILE,
				    "Invalid FW image file format");
		return 0;
	}

	return value;
}

static gboolean
fu_thunderbolt_firmware_update_parse(FuFirmware *firmware,
				     GBytes *fw,
				     gsize offset_ignored,
				     FwupdInstallFlags flags,
				     GError **error)
{
	FuThunderboltFirmwareUpdate *self = FU_THUNDERBOLT_FIRMWARE_UPDATE(firmware);
	guint32 offset = fu_thunderbolt_firmware_read_farb_pointer(self, error);
	if (offset == 0)
		return FALSE;
	g_debug("detected digital section begins at 0x%x", offset);
	fu_thunderbolt_firmware_set_digital(FU_THUNDERBOLT_FIRMWARE(firmware), offset);

	return TRUE;
}

static void
fu_thunderbolt_firmware_update_init(FuThunderboltFirmwareUpdate *self)
{
}

static void
fu_thunderbolt_firmware_update_class_init(FuThunderboltFirmwareUpdateClass *klass)
{
	FuThunderboltFirmwareClass *klass_firmware = FU_THUNDERBOLT_FIRMWARE_CLASS(klass);
	klass_firmware->parse = fu_thunderbolt_firmware_update_parse;
}

FuThunderboltFirmwareUpdate *
fu_thunderbolt_firmware_update_new(void)
{
	return g_object_new(FU_TYPE_THUNDERBOLT_FIRMWARE_UPDATE, NULL);
}
