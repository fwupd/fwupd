/*
 * Copyright (C) 2021 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include "fu-common.h"

#include "fu-synaptics-mst-connection.h"
#include "fu-synaptics-mst-firmware.h"

struct _FuSynapticsMstFirmware {
	FuFirmwareClass		 parent_instance;
	guint16			 board_id;
};

G_DEFINE_TYPE (FuSynapticsMstFirmware, fu_synaptics_mst_firmware, FU_TYPE_FIRMWARE)

guint16
fu_synaptics_mst_firmware_get_board_id (FuSynapticsMstFirmware *self)
{
	g_return_val_if_fail (FU_IS_SYNAPTICS_MST_FIRMWARE (self), 0);
	return self->board_id;
}

static void
fu_synaptics_mst_firmware_to_string (FuFirmware *firmware, guint idt, GString *str)
{
	FuSynapticsMstFirmware *self = FU_SYNAPTICS_MST_FIRMWARE (firmware);
	fu_common_string_append_kx (str, idt, "BoardId", self->board_id);
}

static gboolean
fu_synaptics_mst_firmware_parse (FuFirmware *firmware,
				 GBytes *fw,
				 guint64 addr_start,
				 guint64 addr_end,
				 FwupdInstallFlags flags,
				 GError **error)
{
	FuSynapticsMstFirmware *self = FU_SYNAPTICS_MST_FIRMWARE (firmware);
	const guint8 *buf;
	gsize bufsz;
	g_autoptr(FuFirmwareImage) img = fu_firmware_image_new (fw);
	buf = g_bytes_get_data (fw, &bufsz);
	if (!fu_common_read_uint16_safe (buf, bufsz, ADDR_CUSTOMER_ID,
					 &self->board_id, G_BIG_ENDIAN,
					 error))
		return FALSE;
	fu_firmware_add_image (firmware, img);
	return TRUE;
}

static void
fu_synaptics_mst_firmware_init (FuSynapticsMstFirmware *self)
{
}

static void
fu_synaptics_mst_firmware_class_init (FuSynapticsMstFirmwareClass *klass)
{
	FuFirmwareClass *klass_firmware = FU_FIRMWARE_CLASS (klass);
	klass_firmware->parse = fu_synaptics_mst_firmware_parse;
	klass_firmware->to_string = fu_synaptics_mst_firmware_to_string;
}

FuFirmware *
fu_synaptics_mst_firmware_new (void)
{
	return FU_FIRMWARE (g_object_new (FU_TYPE_SYNAPTICS_MST_FIRMWARE, NULL));
}
