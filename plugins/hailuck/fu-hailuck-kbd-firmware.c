/*
 * Copyright (C) 2020 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include "fu-common.h"

#include "fu-hailuck-kbd-firmware.h"

struct _FuHaiLuckKbdFirmware {
	FuIhexFirmwareClass	 parent_instance;
};

G_DEFINE_TYPE (FuHaiLuckKbdFirmware, fu_hailuck_kbd_firmware, FU_TYPE_IHEX_FIRMWARE)

static gboolean
fu_hailuck_kbd_firmware_parse (FuFirmware *firmware,
			       GBytes *fw,
			       guint64 addr_start,
			       guint64 addr_end,
			       FwupdInstallFlags flags,
			       GError **error)
{
	GPtrArray *records = fu_ihex_firmware_get_records (FU_IHEX_FIRMWARE (firmware));
	g_autoptr(FuFirmwareImage) img = NULL;
	g_autoptr(GByteArray) buf = g_byte_array_new ();
	g_autoptr(GBytes) fw_new = NULL;

	for (guint j = 0; j < records->len; j++) {
		FuIhexFirmwareRecord *rcd = g_ptr_array_index (records, j);
		if (rcd->record_type == FU_IHEX_FIRMWARE_RECORD_TYPE_EOF)
			break;
		if (rcd->record_type != FU_IHEX_FIRMWARE_RECORD_TYPE_DATA) {
			g_set_error (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_NOT_SUPPORTED,
				     "only record 0x0 supported, got 0x%02x",
				     rcd->record_type);
			return FALSE;
		}
		if (rcd->addr + rcd->data->len > buf->len)
			fu_byte_array_set_size (buf, rcd->addr + rcd->data->len);
		if (!fu_memcpy_safe (buf->data, buf->len, rcd->addr,
				     rcd->data->data, rcd->data->len, 0x0,
				     rcd->data->len, error))
			return FALSE;
	}

	/* set the main function executed on system init */
	if (buf->len > 0x37FD && buf->data[1] == 0x38 && buf->data[2] == 0x00) {
		buf->data[0] = buf->data[0x37FB];
		buf->data[1] = buf->data[0x37FC];
		buf->data[2] = buf->data[0x37FD];
		buf->data[0x37FB] = 0x00;
		buf->data[0x37FC] = 0x00;
		buf->data[0x37FD] = 0x00;
	}

	/* whole image */
	fw_new = g_byte_array_free_to_bytes (g_steal_pointer (&buf));
	img = fu_firmware_image_new (fw_new);
	fu_firmware_add_image (firmware, img);
	return TRUE;
}

static void
fu_hailuck_kbd_firmware_init (FuHaiLuckKbdFirmware *self)
{
}

static void
fu_hailuck_kbd_firmware_class_init (FuHaiLuckKbdFirmwareClass *klass)
{
	FuFirmwareClass *klass_firmware = FU_FIRMWARE_CLASS (klass);
	klass_firmware->parse = fu_hailuck_kbd_firmware_parse;
}

FuFirmware *
fu_hailuck_kbd_firmware_new (void)
{
	return FU_FIRMWARE (g_object_new (FU_TYPE_HAILUCK_KBD_FIRMWARE, NULL));
}
