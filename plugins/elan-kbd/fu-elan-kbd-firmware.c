/*
 * Copyright 2025 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include "fu-elan-kbd-common.h"
#include "fu-elan-kbd-firmware.h"
#include "fu-elan-kbd-struct.h"

struct _FuElanKbdFirmware {
	FuFirmware parent_instance;
};

G_DEFINE_TYPE(FuElanKbdFirmware, fu_elan_kbd_firmware, FU_TYPE_FIRMWARE)

static gboolean
fu_elan_kbd_firmware_validate(FuFirmware *firmware,
			      GInputStream *stream,
			      gsize offset,
			      GError **error)
{
	return fu_struct_elan_kbd_firmware_validate_stream(stream, offset, error);
}

static gboolean
fu_elan_kbd_firmware_parse(FuFirmware *firmware,
			   GInputStream *stream,
			   FwupdInstallFlags flags,
			   GError **error)
{
	g_autoptr(FuFirmware) firmware_app = fu_firmware_new();
	g_autoptr(FuFirmware) firmware_bootloader = fu_firmware_new();
	g_autoptr(FuFirmware) firmware_option = fu_firmware_new();
	g_autoptr(GInputStream) stream_app = NULL;
	g_autoptr(GInputStream) stream_bootloader = NULL;
	g_autoptr(GInputStream) stream_option = NULL;

	/* bootloader */
	stream_bootloader = fu_partial_input_stream_new(stream,
							FU_ELAN_KBD_DEVICE_ADDR_BOOT,
							FU_ELAN_KBD_DEVICE_SIZE_BOOT,
							error);
	if (stream_bootloader == NULL)
		return FALSE;
	if (!fu_firmware_set_stream(firmware_bootloader, stream_bootloader, error))
		return FALSE;
	fu_firmware_set_idx(firmware_bootloader, FU_ELAN_KBD_FIRMWARE_IDX_BOOTLOADER);
	fu_firmware_add_image(firmware, firmware_bootloader);

	/* app */
	stream_app = fu_partial_input_stream_new(stream,
						 FU_ELAN_KBD_DEVICE_ADDR_APP,
						 FU_ELAN_KBD_DEVICE_SIZE_APP,
						 error);
	if (stream_app == NULL)
		return FALSE;
	if (!fu_firmware_set_stream(firmware_app, stream_app, error))
		return FALSE;
	fu_firmware_set_idx(firmware_app, FU_ELAN_KBD_FIRMWARE_IDX_APP);
	fu_firmware_add_image(firmware, firmware_app);

	/* option */
	stream_option = fu_partial_input_stream_new(stream,
						    FU_ELAN_KBD_DEVICE_ADDR_OPTION,
						    FU_ELAN_KBD_DEVICE_SIZE_OPTION,
						    error);
	if (stream_option == NULL)
		return FALSE;
	if (!fu_firmware_set_stream(firmware_option, stream_option, error))
		return FALSE;
	fu_firmware_set_idx(firmware_option, FU_ELAN_KBD_FIRMWARE_IDX_OPTION);
	fu_firmware_add_image(firmware, firmware_option);

	/* success */
	return TRUE;
}

static GByteArray *
fu_elan_kbd_firmware_write(FuFirmware *firmware, GError **error)
{
	g_autoptr(GByteArray) buf = g_byte_array_new();
	g_autoptr(GBytes) blob_app = NULL;
	g_autoptr(GBytes) blob_bootloader = NULL;
	g_autoptr(GBytes) blob_option = NULL;

	/* bootloader */
	blob_bootloader = fu_firmware_get_image_by_idx_bytes(firmware,
							     FU_ELAN_KBD_FIRMWARE_IDX_BOOTLOADER,
							     error);
	if (blob_bootloader == NULL)
		return NULL;
	fu_byte_array_append_bytes(buf, blob_bootloader);

	/* app */
	blob_app =
	    fu_firmware_get_image_by_idx_bytes(firmware, FU_ELAN_KBD_FIRMWARE_IDX_APP, error);
	if (blob_app == NULL)
		return NULL;
	fu_byte_array_append_bytes(buf, blob_app);

	/* option */
	blob_option =
	    fu_firmware_get_image_by_idx_bytes(firmware, FU_ELAN_KBD_FIRMWARE_IDX_OPTION, error);
	if (blob_option == NULL)
		return NULL;
	fu_byte_array_append_bytes(buf, blob_option);

	/* success */
	return g_steal_pointer(&buf);
}

static void
fu_elan_kbd_firmware_init(FuElanKbdFirmware *self)
{
}

static void
fu_elan_kbd_firmware_class_init(FuElanKbdFirmwareClass *klass)
{
	FuFirmwareClass *firmware_class = FU_FIRMWARE_CLASS(klass);
	firmware_class->validate = fu_elan_kbd_firmware_validate;
	firmware_class->parse = fu_elan_kbd_firmware_parse;
	firmware_class->write = fu_elan_kbd_firmware_write;
}

FuFirmware *
fu_elan_kbd_firmware_new(void)
{
	return FU_FIRMWARE(g_object_new(FU_TYPE_ELAN_KBD_FIRMWARE, NULL));
}
