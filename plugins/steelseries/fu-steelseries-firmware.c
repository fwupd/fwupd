/*
 * Copyright 2022 Gaël PORTAY <gael.portay@collabora.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include "fu-steelseries-firmware.h"

struct _FuSteelseriesFirmware {
	FuFirmwareClass parent_instance;
	guint32 checksum;
};

G_DEFINE_TYPE(FuSteelseriesFirmware, fu_steelseries_firmware, FU_TYPE_FIRMWARE)

static gboolean
fu_steelseries_firmware_parse(FuFirmware *firmware,
			      GInputStream *stream,
			      gsize offset,
			      FwupdInstallFlags flags,
			      GError **error)
{
	FuSteelseriesFirmware *self = FU_STEELSERIES_FIRMWARE(firmware);
	guint32 checksum_tmp = 0xFFFFFFFF;
	guint32 checksum = 0;
	gsize streamsz = 0;
	g_autoptr(GInputStream) stream_tmp = NULL;

	if (!fu_input_stream_size(stream, &streamsz, error))
		return FALSE;
	if (streamsz < sizeof(checksum)) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INVALID_DATA,
				    "image is too small");
		return FALSE;
	}
	if (!fu_input_stream_read_u32(stream,
				      streamsz - sizeof(checksum),
				      &checksum,
				      G_LITTLE_ENDIAN,
				      error))
		return FALSE;

	stream_tmp = fu_partial_input_stream_new(stream, 0, streamsz - sizeof(checksum_tmp), error);
	if (stream_tmp == NULL)
		return FALSE;
	if (!fu_input_stream_compute_crc32(stream_tmp,
					   FU_CRC_KIND_B32_STANDARD,
					   &checksum_tmp,
					   error))
		return FALSE;
	if (checksum_tmp != checksum) {
		if ((flags & FWUPD_INSTALL_FLAG_IGNORE_CHECKSUM) == 0) {
			g_set_error(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INTERNAL,
				    "checksum mismatch, got 0x%08x, expected 0x%08x",
				    checksum_tmp,
				    checksum);
			return FALSE;
		}
		g_debug("ignoring checksum mismatch, got 0x%08x, expected 0x%08x",
			checksum_tmp,
			checksum);
	}

	self->checksum = checksum;
	fu_firmware_add_flag(FU_FIRMWARE(self), FU_FIRMWARE_FLAG_HAS_CHECKSUM);

	/* success */
	return TRUE;
}

static void
fu_steelseries_firmware_export(FuFirmware *firmware, FuFirmwareExportFlags flags, XbBuilderNode *bn)
{
	FuSteelseriesFirmware *self = FU_STEELSERIES_FIRMWARE(firmware);

	fu_xmlb_builder_insert_kx(bn, "checksum", self->checksum);
}

static void
fu_steelseries_firmware_init(FuSteelseriesFirmware *self)
{
}

static void
fu_steelseries_firmware_class_init(FuSteelseriesFirmwareClass *klass)
{
	FuFirmwareClass *firmware_class = FU_FIRMWARE_CLASS(klass);
	firmware_class->parse = fu_steelseries_firmware_parse;
	firmware_class->export = fu_steelseries_firmware_export;
}

FuFirmware *
fu_steelseries_firmware_new(void)
{
	return FU_FIRMWARE(g_object_new(FU_TYPE_STEELSERIES_FIRMWARE, NULL));
}
