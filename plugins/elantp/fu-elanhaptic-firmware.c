/*
 * Copyright (C) 2022 Jingle Wu <jingle.wu@emc.com.tw>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include <fwupdplugin.h>
#include <stdio.h>
#include "fu-elantp-common.h"
#include "fu-elanhaptic-firmware.h"

struct _FuElanhapticFirmware {
	FuFirmwareClass parent_instance;
	guint16 eeprom_driver_ic;
};

G_DEFINE_TYPE(FuElanhapticFirmware, fu_elanhaptic_firmware, FU_TYPE_FIRMWARE)


const guint8 elanhaptic_signature_ictype02[] = {0xFF, 0x40, 0xA2, 0x5B};

guint16
fu_elanhaptic_firmware_get_driveric(FuElanhapticFirmware *self)
{
	g_return_val_if_fail(FU_IS_ELANHAPTIC_FIRMWARE(self), 0);
	return self->eeprom_driver_ic;
}

static void
fu_elanhaptic_firmware_export(FuFirmware *firmware, FuFirmwareExportFlags flags, XbBuilderNode *bn)
{
	FuElanhapticFirmware *self = FU_ELANHAPTIC_FIRMWARE(firmware);
	fu_xmlb_builder_insert_kx(bn, "eeprom_driver_ic", self->eeprom_driver_ic);
}

static gboolean
fu_elanhaptic_firmware_check_magic(FuFirmware *firmware, GBytes *fw, gsize offset, GError **error)
{
	FuElanhapticFirmware *self = FU_ELANHAPTIC_FIRMWARE(firmware);
	gsize bufsz = g_bytes_get_size(fw);
	const guint8 *buf = g_bytes_get_data(fw, NULL);
	for (gsize i = 0; i < sizeof(elanhaptic_signature_ictype02); i++) {
		guint8 tmp = 0x0;
		if (!fu_memread_uint8_safe(buf,
					   bufsz,
					   i,
					   &tmp,
					   error)) 
			return FALSE;
		if (tmp != elanhaptic_signature_ictype02[i]) {
			g_set_error(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INVALID_FILE,
				    "signature[%u] invalid: got 0x%2x, expected 0x%02x",
				    (guint)i,
				    tmp,
				    elanhaptic_signature_ictype02[i]);
			return FALSE;
		}
	}
	self->eeprom_driver_ic = 0x2;
	
	return TRUE;	
}

static gboolean
fu_elanhaptic_firmware_parse(FuFirmware *firmware,
			 GBytes *fw,
			 gsize offset,
			 FwupdInstallFlags flags,
			 GError **error)
{
	FuElanhapticFirmware *self = FU_ELANHAPTIC_FIRMWARE(firmware);
	gsize bufsz = 0;
	guint8 v_s = 0;
    	guint8 v_d = 0;
    	guint8 v_m = 0;
    	guint8 v_y = 0;
    	guint8 tmp = 0;
    	g_autofree gchar *version_str = NULL;
	const guint8 *buf = g_bytes_get_data(fw, &bufsz);

	g_return_val_if_fail(fw != NULL, FALSE);
	
	if (bufsz < 6)
		return FALSE;
	
	if (!fu_memread_uint8_safe(buf, bufsz, 0x4, &tmp, error))
		return FALSE;
	v_m = tmp & 0xF;
	v_s = (tmp & 0xF0) >> 4;
	
	if (!fu_memread_uint8_safe(buf, bufsz, 0x5, &v_d, error))
		return FALSE;
	if (!fu_memread_uint8_safe(buf, bufsz, 0x6, &v_y, error))
		return FALSE;

	if (v_y == 0xFF || v_d == 0xFF || v_m == 0xF) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INVALID_FILE,
			    "bad firmware version %02d%02d%02d%02d", 
			    v_y, v_m, v_d, v_s);
		return FALSE;
	}

	version_str = g_strdup_printf("%02d%02d%02d%02d", v_y, v_m, v_d, v_s);
	fu_firmware_set_version(FU_FIRMWARE(self), version_str);
	
	/* success */
	return TRUE;
}

static gboolean
fu_elanhaptic_firmware_build(FuFirmware *firmware, XbNode *n, GError **error)
{
	FuElanhapticFirmware *self = FU_ELANHAPTIC_FIRMWARE(firmware);
	guint64 tmp;

	/* two simple properties */
	tmp = xb_node_query_text_as_uint(n, "eeprom_driver_ic", NULL);
	if (tmp != G_MAXUINT64 && tmp <= G_MAXUINT16)
		self->eeprom_driver_ic = tmp;

	/* success */
	return TRUE;
}

static GBytes *
fu_elanhaptic_firmware_write(FuFirmware *firmware, GError **error)
{
	g_autoptr(GByteArray) buf = g_byte_array_new();
	g_autoptr(GBytes) blob = NULL;

	/* only one image supported */
	blob = fu_firmware_get_bytes_with_patches(firmware, error);
	if (blob == NULL)
		return NULL;

	fu_byte_array_append_bytes(buf, blob);

	return g_byte_array_free_to_bytes(g_steal_pointer(&buf));
}

static void
fu_elanhaptic_firmware_init(FuElanhapticFirmware *self)
{
}

static void
fu_elanhaptic_firmware_class_init(FuElanhapticFirmwareClass *klass)
{
	FuFirmwareClass *klass_firmware = FU_FIRMWARE_CLASS(klass);
	klass_firmware->check_magic = fu_elanhaptic_firmware_check_magic;
	klass_firmware->parse = fu_elanhaptic_firmware_parse;
	klass_firmware->build = fu_elanhaptic_firmware_build;
	klass_firmware->write = fu_elanhaptic_firmware_write;
	klass_firmware->export = fu_elanhaptic_firmware_export;
}

FuFirmware *
fu_elanhaptic_firmware_new(void)
{
	return FU_FIRMWARE(g_object_new(FU_TYPE_ELANHAPTIC_FIRMWARE, NULL));
}
