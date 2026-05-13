/*
 * Copyright 2022 Jingle Wu <jingle.wu@emc.com.tw>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include "fu-elantp-common.h"
#include "fu-elantp-haptic-firmware.h"
#include "fu-elantp-struct.h"

struct _FuElantpHapticFirmware {
	FuFirmware parent_instance;
	guint16 driver_ic;
};

G_DEFINE_TYPE(FuElantpHapticFirmware, fu_elantp_haptic_firmware, FU_TYPE_FIRMWARE)

guint16
fu_elantp_haptic_firmware_get_driver_ic(FuElantpHapticFirmware *self)
{
	g_return_val_if_fail(FU_IS_ELANTP_HAPTIC_FIRMWARE(self), 0);
	return self->driver_ic;
}

static void
fu_elantp_haptic_firmware_export(FuFirmware *firmware,
				 FuFirmwareExportFlags flags,
				 XbBuilderNode *bn)
{
	FuElantpHapticFirmware *self = FU_ELANTP_HAPTIC_FIRMWARE(firmware);
	fu_xmlb_builder_insert_kx(bn, "driver_ic", self->driver_ic);
}

static gboolean
fu_elantp_haptic_firmware_validate(FuFirmware *firmware,
				   GInputStream *stream,
				   gsize offset,
				   GError **error)
{
	return fu_struct_elantp_haptic_firmware_hdr_validate_stream(stream, offset, error);
}

static gboolean
fu_elantp_haptic_firmware_parse(FuFirmware *firmware,
				GInputStream *stream,
				FuFirmwareParseFlags flags,
				GError **error)
{
	FuElantpHapticFirmware *self = FU_ELANTP_HAPTIC_FIRMWARE(firmware);
	g_autofree gchar *version_str = NULL;
	g_autoptr(FuStructElantpHapticFirmwareHdr) st = NULL;

	st = fu_struct_elantp_haptic_firmware_hdr_parse_stream(stream, 0x0, error);
	if (st == NULL)
		return FALSE;
	if (fu_struct_elantp_haptic_firmware_hdr_get_ver_y(st) == 0xFF ||
	    fu_struct_elantp_haptic_firmware_hdr_get_ver_d(st) == 0xFF ||
	    fu_struct_elantp_haptic_firmware_hdr_get_ver_sm(st) == 0xFF) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INVALID_FILE,
				    "bad firmware version");
		return FALSE;
	}
	version_str = g_strdup_printf("%02d%02d%02d%02d",
				      fu_struct_elantp_haptic_firmware_hdr_get_ver_y(st),
				      fu_struct_elantp_haptic_firmware_hdr_get_ver_sm(st) & 0xF,
				      fu_struct_elantp_haptic_firmware_hdr_get_ver_d(st),
				      fu_struct_elantp_haptic_firmware_hdr_get_ver_sm(st) >> 4);
	fu_firmware_set_version(FU_FIRMWARE(self), version_str);

	/* success */
	self->driver_ic = 0x2;
	return TRUE;
}

static void
fu_elantp_haptic_firmware_init(FuElantpHapticFirmware *self)
{
}

static void
fu_elantp_haptic_firmware_class_init(FuElantpHapticFirmwareClass *klass)
{
	FuFirmwareClass *firmware_class = FU_FIRMWARE_CLASS(klass);
	firmware_class->validate = fu_elantp_haptic_firmware_validate;
	firmware_class->parse = fu_elantp_haptic_firmware_parse;
	firmware_class->export = fu_elantp_haptic_firmware_export;
}
