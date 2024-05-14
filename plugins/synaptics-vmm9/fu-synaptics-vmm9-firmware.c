/*
 * Copyright 2024 Richard hughes <Richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include <fwupdplugin.h>

#include "fu-synaptics-vmm9-firmware.h"
#include "fu-synaptics-vmm9-struct.h"

struct _FuSynapticsVmm9Firmware {
	FuFirmware parent_instance;
	guint8 board_id;
	guint8 customer_id;
};

G_DEFINE_TYPE(FuSynapticsVmm9Firmware, fu_synaptics_vmm9_firmware, FU_TYPE_FIRMWARE)

#define FU_SYNAPTICS_VMM9_FIRMWARE_OFFSET_CUSTOMER_ID 0x0000620E
#define FU_SYNAPTICS_VMM9_FIRMWARE_OFFSET_BOARD_ID    0x0000620F
#define FU_SYNAPTICS_VMM9_FIRMWARE_OFFSET_VERSION     0x0000E000

static void
fu_synaptics_vmm9_firmware_export(FuFirmware *firmware,
				  FuFirmwareExportFlags flags,
				  XbBuilderNode *bn)
{
	FuSynapticsVmm9Firmware *self = FU_SYNAPTICS_VMM9_FIRMWARE(firmware);
	fu_xmlb_builder_insert_kx(bn, "board_id", self->board_id);
	fu_xmlb_builder_insert_kx(bn, "customer_id", self->customer_id);
}

static gboolean
fu_synaptics_vmm9_validate(FuFirmware *firmware, GBytes *fw, gsize offset, GError **error)
{
	return fu_struct_synaptics_vmm9_validate_bytes(fw, offset, error);
}

static gboolean
fu_synaptics_vmm9_firmware_parse(FuFirmware *firmware,
				 GBytes *fw,
				 gsize offset,
				 FwupdInstallFlags flags,
				 GError **error)
{
	FuSynapticsVmm9Firmware *self = FU_SYNAPTICS_VMM9_FIRMWARE(firmware);
	g_autoptr(GByteArray) st_hdr = NULL;
	gsize bufsz = 0;
	guint8 version_major = 0;
	guint8 version_minor = 0;
	guint16 version_micro = 0;
	g_autofree gchar *version = NULL;
	const guint8 *buf = g_bytes_get_data(fw, &bufsz);

	/* verify header */
	st_hdr = fu_struct_synaptics_vmm9_parse_bytes(fw, offset, error);
	if (st_hdr == NULL)
		return FALSE;

	/* read version */
	if (!fu_memread_uint8_safe(buf,
				   bufsz,
				   FU_SYNAPTICS_VMM9_FIRMWARE_OFFSET_VERSION + 0x0,
				   &version_major,
				   error))
		return FALSE;
	if (!fu_memread_uint8_safe(buf,
				   bufsz,
				   FU_SYNAPTICS_VMM9_FIRMWARE_OFFSET_VERSION + 0x1,
				   &version_minor,
				   error))
		return FALSE;
	if (!fu_memread_uint16_safe(buf,
				    bufsz,
				    FU_SYNAPTICS_VMM9_FIRMWARE_OFFSET_VERSION + 0x2,
				    &version_micro,
				    G_LITTLE_ENDIAN,
				    error))
		return FALSE;
	version = g_strdup_printf("%u.%02u.%03u", version_major, version_minor, version_micro);
	fu_firmware_set_version(firmware, version);

	/* board and customer IDs */
	if (!fu_memread_uint8_safe(buf,
				   bufsz,
				   FU_SYNAPTICS_VMM9_FIRMWARE_OFFSET_BOARD_ID,
				   &self->board_id,
				   error))
		return FALSE;
	if (!fu_memread_uint8_safe(buf,
				   bufsz,
				   FU_SYNAPTICS_VMM9_FIRMWARE_OFFSET_CUSTOMER_ID,
				   &self->customer_id,
				   error))
		return FALSE;

	/* success */
	return TRUE;
}

guint8
fu_synaptics_vmm9_firmware_get_board_id(FuSynapticsVmm9Firmware *self)
{
	g_return_val_if_fail(FU_IS_SYNAPTICS_VMM9_FIRMWARE(self), G_MAXUINT8);
	return self->board_id;
}

guint8
fu_synaptics_vmm9_firmware_get_customer_id(FuSynapticsVmm9Firmware *self)
{
	g_return_val_if_fail(FU_IS_SYNAPTICS_VMM9_FIRMWARE(self), G_MAXUINT8);
	return self->customer_id;
}

static void
fu_synaptics_vmm9_firmware_init(FuSynapticsVmm9Firmware *self)
{
	fu_firmware_add_flag(FU_FIRMWARE(self), FU_FIRMWARE_FLAG_HAS_VID_PID);
}

static void
fu_synaptics_vmm9_firmware_class_init(FuSynapticsVmm9FirmwareClass *klass)
{
	FuFirmwareClass *firmware_class = FU_FIRMWARE_CLASS(klass);
	firmware_class->check_magic = fu_synaptics_vmm9_validate;
	firmware_class->parse = fu_synaptics_vmm9_firmware_parse;
	firmware_class->export = fu_synaptics_vmm9_firmware_export;
}

FuFirmware *
fu_synaptics_vmm9_firmware_new(void)
{
	return FU_FIRMWARE(g_object_new(FU_TYPE_SYNAPTICS_VMM9_FIRMWARE, NULL));
}
