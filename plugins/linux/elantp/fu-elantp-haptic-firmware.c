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
	FuFirmwareClass parent_instance;
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
	guint8 v_s = 0;
	guint8 v_d = 0;
	guint8 v_m = 0;
	guint8 v_y = 0;
	guint8 tmp = 0;
	g_autofree gchar *version_str = NULL;

	if (!fu_input_stream_read_u8(stream, 0x4, &tmp, error))
		return FALSE;
	v_m = tmp & 0xF;
	v_s = (tmp & 0xF0) >> 4;
	if (!fu_input_stream_read_u8(stream, 0x5, &v_d, error))
		return FALSE;
	if (!fu_input_stream_read_u8(stream, 0x6, &v_y, error))
		return FALSE;
	if (v_y == 0xFF || v_d == 0xFF || v_m == 0xF) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INVALID_FILE,
			    "bad firmware version %02d%02d%02d%02d",
			    v_y,
			    v_m,
			    v_d,
			    v_s);
		return FALSE;
	}

	version_str = g_strdup_printf("%02d%02d%02d%02d", v_y, v_m, v_d, v_s);
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

FuFirmware *
fu_elantp_haptic_firmware_new(void)
{
	return FU_FIRMWARE(g_object_new(FU_TYPE_ELANTP_HAPTIC_FIRMWARE, NULL));
}
