/*
 * Copyright 2020 Fresco Logic
 * Copyright 2020 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include "fu-fresco-pd-common.h"
#include "fu-fresco-pd-firmware.h"

struct _FuFrescoPdFirmware {
	FuFirmware parent_instance;
	guint8 customer_id;
};

G_DEFINE_TYPE(FuFrescoPdFirmware, fu_fresco_pd_firmware, FU_TYPE_FIRMWARE)

guint8
fu_fresco_pd_firmware_get_customer_id(FuFrescoPdFirmware *self)
{
	return self->customer_id;
}

static void
fu_fresco_pd_firmware_export(FuFirmware *firmware, FuFirmwareExportFlags flags, XbBuilderNode *bn)
{
	FuFrescoPdFirmware *self = FU_FRESCO_PD_FIRMWARE(firmware);
	fu_xmlb_builder_insert_kx(bn, "customer_id", self->customer_id);
}

static gboolean
fu_fresco_pd_firmware_parse(FuFirmware *firmware,
			    GInputStream *stream,
			    FuFirmwareParseFlags flags,
			    GError **error)
{
	FuFrescoPdFirmware *self = FU_FRESCO_PD_FIRMWARE(firmware);
	guint8 ver[4] = {0x0};
	g_autofree gchar *version = NULL;

	/* read version block */
	if (!fu_input_stream_read_safe(stream,
				       ver,
				       sizeof(ver),
				       0x0,    /* dst */
				       0x1000, /* src */
				       sizeof(ver),
				       error))
		return FALSE;

	/* customer ID is always the 2nd byte */
	self->customer_id = ver[1];

	/* set version number */
	version = fu_fresco_pd_version_from_buf(ver);
	fu_firmware_set_version(firmware, version);
	return TRUE;
}

static void
fu_fresco_pd_firmware_init(FuFrescoPdFirmware *self)
{
	fu_firmware_add_flag(FU_FIRMWARE(self), FU_FIRMWARE_FLAG_NO_AUTO_DETECTION);
}

static void
fu_fresco_pd_firmware_class_init(FuFrescoPdFirmwareClass *klass)
{
	FuFirmwareClass *firmware_class = FU_FIRMWARE_CLASS(klass);
	firmware_class->parse = fu_fresco_pd_firmware_parse;
	firmware_class->export = fu_fresco_pd_firmware_export;
}

FuFirmware *
fu_fresco_pd_firmware_new(void)
{
	return FU_FIRMWARE(g_object_new(FU_TYPE_FRESCO_PD_FIRMWARE, NULL));
}
