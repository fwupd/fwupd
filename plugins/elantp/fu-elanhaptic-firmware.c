/*
 * Copyright (C) 2020 Richard Hughes <richard@hughsie.com>
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
	guint32 eeprom_fw_ver;
};

G_DEFINE_TYPE(FuElanhapticFirmware, fu_elanhaptic_firmware, FU_TYPE_FIRMWARE)


const guint8 elanhaptic_signature_ictype02[] = {0xFF, 0x40, 0xA2, 0x5B};

guint32
fu_elanhaptic_firmware_get_fwver(FuElanhapticFirmware *self)
{
	g_return_val_if_fail(FU_IS_ELANHAPTIC_FIRMWARE(self), 0);
	return self->eeprom_fw_ver;
}

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
	fu_xmlb_builder_insert_kx(bn, "eeprom_fw_ver", self->eeprom_fw_ver);
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
	guint16 v_s = 0;
    	guint16 v_d = 0;
    	guint16 v_m = 0;
    	guint16 v_y = 0;
    	gint8 tmp[256] = {0x0};
	const guint8 *buf = g_bytes_get_data(fw, &bufsz);

	g_return_val_if_fail(fw != NULL, FALSE);
	
	v_d = buf[5];
	v_m = buf[4] & 0xF;
	v_s = (buf[4] & 0xF0) >> 4;
	v_y = buf[6];

	if ((v_y==0xFF) || (v_m==0xFF) || (v_d==0xFF) || (v_s==0xFF))
		return FALSE;

	sprintf((char*)tmp, "%02d%02d%02d%02d", v_y, v_m, v_d, v_s);
	self->eeprom_fw_ver = atoi((char*)tmp);
	
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
	tmp = xb_node_query_text_as_uint(n, "eeprom_fw_ver", NULL);
	if (tmp != G_MAXUINT64 && tmp <= G_MAXUINT32)
		self->eeprom_fw_ver = tmp;

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
