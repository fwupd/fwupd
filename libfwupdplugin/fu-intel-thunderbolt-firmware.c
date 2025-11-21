/*
 * Copyright 2021 Dell Inc.
 * Copyright 2020 Richard Hughes <richard@hughsie.com>
 * Copyright 2020 Mario Limonciello <mario.limonciello@dell.com>
 * Copyright 2017 Intel Corporation.
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#define G_LOG_DOMAIN "FuFirmware"

#include "config.h"

#include "fu-byte-array.h"
#include "fu-input-stream.h"
#include "fu-intel-thunderbolt-firmware.h"
#include "fu-mem.h"
#include "fu-partial-input-stream.h"

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
fu_intel_thunderbolt_firmware_nvm_valid_farb_pointer(guint32 pointer)
{
	return pointer != 0 && pointer != 0xFFFFFF;
}

static gboolean
fu_intel_thunderbolt_firmware_parse(FuFirmware *firmware,
				    GInputStream *stream,
				    FuFirmwareParseFlags flags,
				    GError **error)
{
	const guint32 farb_offsets[] = {0x0, 0x1000};
	gboolean valid = FALSE;
	guint32 farb_pointer = 0x0;
	g_autoptr(GInputStream) partial_stream = NULL;

	/* get header offset */
	for (guint i = 0; i < G_N_ELEMENTS(farb_offsets); i++) {
		if (!fu_input_stream_read_u24(stream,
					      farb_offsets[i],
					      &farb_pointer,
					      G_LITTLE_ENDIAN,
					      error))
			return FALSE;
		if (fu_intel_thunderbolt_firmware_nvm_valid_farb_pointer(farb_pointer)) {
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
	partial_stream = fu_partial_input_stream_new(stream, farb_pointer, G_MAXSIZE, error);
	if (partial_stream == NULL) {
		g_prefix_error(error, "failed to cut from NVM: ");
		return FALSE;
	}
	return FU_FIRMWARE_CLASS(fu_intel_thunderbolt_firmware_parent_class)
	    ->parse(firmware, partial_stream, flags, error);
}

static GByteArray *
fu_intel_thunderbolt_firmware_write(FuFirmware *firmware, GError **error)
{
	g_autoptr(GByteArray) buf = g_byte_array_new();
	g_autoptr(GByteArray) buf_nvm = NULL;

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
	buf_nvm =
	    FU_FIRMWARE_CLASS(fu_intel_thunderbolt_firmware_parent_class)->write(firmware, error);
	if (buf_nvm == NULL)
		return NULL;
	g_byte_array_append(buf, buf_nvm->data, buf_nvm->len);

	/* success */
	return g_steal_pointer(&buf);
}

static void
fu_intel_thunderbolt_firmware_init(FuIntelThunderboltFirmware *self)
{
}

static void
fu_intel_thunderbolt_firmware_class_init(FuIntelThunderboltFirmwareClass *klass)
{
	FuFirmwareClass *firmware_class = FU_FIRMWARE_CLASS(klass);
	firmware_class->parse = fu_intel_thunderbolt_firmware_parse;
	firmware_class->write = fu_intel_thunderbolt_firmware_write;
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
