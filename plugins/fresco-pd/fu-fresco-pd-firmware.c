/*
 * Copyright (C) 2020 Fresco Logic
 * Copyright (C) 2020 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include "fu-fresco-pd-common.h"
#include "fu-fresco-pd-firmware.h"

struct _FuFrescoPdFirmware {
	FuFirmwareClass		 parent_instance;
	guint8			 customer_id;
};

G_DEFINE_TYPE (FuFrescoPdFirmware, fu_fresco_pd_firmware, FU_TYPE_FIRMWARE)

guint8
fu_fresco_pd_firmware_get_customer_id (FuFrescoPdFirmware *self)
{
	return self->customer_id;
}

static void
fu_fresco_pd_firmware_to_string (FuFirmware *firmware, guint idt, GString *str)
{
	FuFrescoPdFirmware *self = FU_FRESCO_PD_FIRMWARE (firmware);
	fu_common_string_append_ku (str, idt, "CustomerID", self->customer_id);
}

static gboolean
fu_fresco_pd_firmware_parse (FuFirmware *firmware,
			     GBytes *fw,
			     guint64 addr_start,
			     guint64 addr_end,
			     FwupdInstallFlags flags,
			     GError **error)
{
	FuFrescoPdFirmware *self = FU_FRESCO_PD_FIRMWARE (firmware);
	guint8 ver[4] = { 0x0 };
	gsize bufsz = 0;
	const guint8 *buf = g_bytes_get_data (fw, &bufsz);
	g_autofree gchar *version = NULL;
	g_autoptr(FuFirmwareImage) img = fu_firmware_image_new (fw);

	/* read version block */
	if (!fu_memcpy_safe (ver, sizeof(ver), 0x0,	/* dst */
			     buf, bufsz, 0x1000,	/* src */
			     sizeof(ver), error))
		return FALSE;

	/* customer ID is always the 2nd byte */
	self->customer_id = ver[1];

	/* set version number */
	version = fu_fresco_pd_version_from_buf (ver);
	fu_firmware_image_set_version (img, version);
	fu_firmware_add_image (firmware, img);
	return TRUE;
}

static void
fu_fresco_pd_firmware_init (FuFrescoPdFirmware *self)
{
}

static void
fu_fresco_pd_firmware_class_init (FuFrescoPdFirmwareClass *klass)
{
	FuFirmwareClass *klass_firmware = FU_FIRMWARE_CLASS (klass);
	klass_firmware->parse = fu_fresco_pd_firmware_parse;
	klass_firmware->to_string = fu_fresco_pd_firmware_to_string;
}

FuFirmware *
fu_fresco_pd_firmware_new (void)
{
	return FU_FIRMWARE (g_object_new (FU_TYPE_FRESCO_PD_FIRMWARE, NULL));
}
