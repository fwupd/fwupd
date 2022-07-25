/*
 * Copyright (C) 2021 Dell Inc.
 * Copyright (C) 2020 Richard Hughes <richard@hughsie.com>
 * Copyright (C) 2020 Mario Limonciello <mario.limonciello@dell.com>
 * Copyright (C) 2017 Intel Corporation.
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#define G_LOG_DOMAIN "FuFirmware"

#include "config.h"

#include "fu-byte-array.h"
#include "fu-intel-thunderbolt-firmware.h"
#include "fu-mem.h"

/**
 * FuIntelThunderboltFirmware:
 *
 * The Non-Volatile-Memory file-format specification. This is what you would find as the update
 * payload.
 *
 * See also: [class@FuFirmware]
 */

G_DEFINE_TYPE(FuIntelThunderboltFirmware,
	      fu_intel_thunderbolt_firmware,
	      FU_TYPE_INTEL_THUNDERBOLT_NVM)

static gboolean
fu_intel_thunderbolt_nvm_valid_farb_pointer(guint32 pointer)
{
	return pointer != 0 && pointer != 0xFFFFFF;
}

static gboolean
fu_intel_thunderbolt_firmware_parse(FuFirmware *firmware,
				    GBytes *fw,
				    gsize offset,
				    FwupdInstallFlags flags,
				    GError **error)
{
	const guint32 farb_offsets[] = {0x0, 0x1000};
	gboolean valid = FALSE;
	guint32 farb_pointer = 0x0;

	/* get header offset */
	for (guint i = 0; i < G_N_ELEMENTS(farb_offsets); i++) {
		if (!fu_memread_uint24_safe(g_bytes_get_data(fw, NULL),
					    g_bytes_get_size(fw),
					    offset + farb_offsets[i],
					    &farb_pointer,
					    G_LITTLE_ENDIAN,
					    error))
			return FALSE;
		if (fu_intel_thunderbolt_nvm_valid_farb_pointer(farb_pointer)) {
			valid = TRUE;
			break;
		}
	}
	if (!valid) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INVALID_FILE,
				    "no valid farb pointer found");
		return FALSE;
	}
	g_debug("detected digital section begins at 0x%x", farb_pointer);
	fu_firmware_set_offset(firmware, farb_pointer);

	/* FuIntelThunderboltNvm->parse */
	return FU_FIRMWARE_CLASS(fu_intel_thunderbolt_firmware_parent_class)
	    ->parse(firmware, fw, offset + farb_pointer, flags, error);
}

static GBytes *
fu_intel_thunderbolt_firmware_write(FuFirmware *firmware, GError **error)
{
	g_autoptr(GByteArray) buf = g_byte_array_new();
	g_autoptr(GBytes) blob = NULL;

	/* sanity check */
	if (fu_firmware_get_offset(firmware) < 0x08) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INVALID_FILE,
				    "not valid offset");
		return NULL;
	}

	/* offset */
	fu_byte_array_append_uint32(buf, fu_firmware_get_offset(firmware), G_LITTLE_ENDIAN);
	fu_byte_array_set_size(buf, fu_firmware_get_offset(firmware), 0x00);

	/* FuIntelThunderboltNvm->write */
	blob =
	    FU_FIRMWARE_CLASS(fu_intel_thunderbolt_firmware_parent_class)->write(firmware, error);
	fu_byte_array_append_bytes(buf, blob);

	/* success */
	return g_byte_array_free_to_bytes(g_steal_pointer(&buf));
}

static void
fu_intel_thunderbolt_firmware_init(FuIntelThunderboltFirmware *self)
{
}

static void
fu_intel_thunderbolt_firmware_class_init(FuIntelThunderboltFirmwareClass *klass)
{
	FuFirmwareClass *klass_firmware = FU_FIRMWARE_CLASS(klass);
	klass_firmware->parse = fu_intel_thunderbolt_firmware_parse;
	klass_firmware->write = fu_intel_thunderbolt_firmware_write;
}

/**
 * fu_intel_thunderbolt_firmware_new:
 *
 * Creates a new #FuFirmware of Intel NVM format
 *
 * Since: 1.8.5
 **/
FuFirmware *
fu_intel_thunderbolt_firmware_new(void)
{
	return FU_FIRMWARE(g_object_new(FU_TYPE_INTEL_THUNDERBOLT_FIRMWARE, NULL));
}
