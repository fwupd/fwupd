/*
 * Copyright (C) 2020 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include "fu-common.h"

#include "fu-elantp-common.h"
#include "fu-elantp-firmware.h"

struct _FuElantpFirmware {
	FuFirmwareClass		 parent_instance;
	guint16			 module_id;
	guint16			 iap_addr;
};

G_DEFINE_TYPE (FuElantpFirmware, fu_elantp_firmware, FU_TYPE_FIRMWARE)

/* firmware block update */
#define ETP_IAP_START_ADDR		0x0083

guint16
fu_elantp_firmware_get_module_id (FuElantpFirmware *self)
{
	g_return_val_if_fail (FU_IS_ELANTP_FIRMWARE (self), 0);
	return self->module_id;
}

guint16
fu_elantp_firmware_get_iap_addr (FuElantpFirmware *self)
{
	g_return_val_if_fail (FU_IS_ELANTP_FIRMWARE (self), 0);
	return self->iap_addr;
}

static void
fu_elantp_firmware_to_string (FuFirmware *firmware, guint idt, GString *str)
{
	FuElantpFirmware *self = FU_ELANTP_FIRMWARE (firmware);
	fu_common_string_append_kx (str, idt, "IapAddr", self->iap_addr);
	fu_common_string_append_kx (str, idt, "ModuleId", self->module_id);
}

static gboolean
fu_elantp_firmware_parse (FuFirmware *firmware,
			  GBytes *fw,
			  guint64 addr_start,
			  guint64 addr_end,
			  FwupdInstallFlags flags,
			  GError **error)
{
	FuElantpFirmware *self = FU_ELANTP_FIRMWARE (firmware);
	gsize bufsz = 0;
	guint16 iap_addr_wrds;
	guint16 module_id_wrds;
	const guint8 *buf = g_bytes_get_data (fw, &bufsz);
	g_autoptr(FuFirmwareImage) img = fu_firmware_image_new (fw);

	/* presumably in words */
	if (!fu_common_read_uint16_safe (buf, bufsz, ETP_IAP_START_ADDR * 2,
					 &iap_addr_wrds, G_LITTLE_ENDIAN, error))
		return FALSE;
	self->iap_addr = iap_addr_wrds * 2;

	/* read module ID */
	if (!fu_common_read_uint16_safe (buf, bufsz, self->iap_addr,
					 &module_id_wrds, G_LITTLE_ENDIAN, error))
		return FALSE;
	if (!fu_common_read_uint16_safe (buf, bufsz, module_id_wrds * 2,
					 &self->module_id, G_LITTLE_ENDIAN, error))
		return FALSE;

	/* whole image */
	fu_firmware_add_image (firmware, img);
	return TRUE;
}

static void
fu_elantp_firmware_init (FuElantpFirmware *self)
{
}

static void
fu_elantp_firmware_class_init (FuElantpFirmwareClass *klass)
{
	FuFirmwareClass *klass_firmware = FU_FIRMWARE_CLASS (klass);
	klass_firmware->parse = fu_elantp_firmware_parse;
	klass_firmware->to_string = fu_elantp_firmware_to_string;
}

FuFirmware *
fu_elantp_firmware_new (void)
{
	return FU_FIRMWARE (g_object_new (FU_TYPE_ELANTP_FIRMWARE, NULL));
}
