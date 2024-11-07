/*
 * Copyright 2024 Dell Technologies
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later OR MIT
 */

#include "config.h"

#include "fu-dell-kestrel-common.h"
#include "fu-dell-kestrel-rtshub-firmware.h"

#define DOCK_RTSHUB_GEN2_VERSION_OFFSET 0x7F52
#define DOCK_RTSHUB_GEN1_VERSION_OFFSET 0x7FA6
#define DOCK_RTSHUB_GEN1_VID_OFFSET	0x7FA8
#define DOCK_RTSHUB_GEN1_PID_OFFSET	0x7FAA

struct _FuDellKestrelRtshubFirmware {
	FuFirmwareClass parent_instance;
	guint16 pid;
};

G_DEFINE_TYPE(FuDellKestrelRtshubFirmware, fu_dell_kestrel_rtshub_firmware, FU_TYPE_FIRMWARE)

static gchar *
fu_dell_kestrel_rtshub_firmware_convert_version(FuFirmware *firmware, guint64 version_raw)
{
	return fu_version_from_uint32_hex(version_raw, fu_firmware_get_version_format(firmware));
}

static void
fu_dell_kestrel_rtshub_firmware_export(FuFirmware *firmware,
				       FuFirmwareExportFlags flags,
				       XbBuilderNode *bn)
{
	FuDellKestrelRtshubFirmware *self = FU_DELL_KESTREL_RTSHUB_FIRMWARE(firmware);
	fu_xmlb_builder_insert_kx(bn, "product_id", self->pid);
}

static gboolean
fu_dell_kestrel_rtshub_firmware_set_offset(GInputStream *stream,
					   guint16 *version_offset,
					   guint16 *pid_offset,
					   GError **error)
{
	guint16 vid_raw = 0;

	if (!fu_input_stream_read_u16(stream,
				      DOCK_RTSHUB_GEN1_VID_OFFSET,
				      &vid_raw,
				      G_BIG_ENDIAN,
				      error))
		return FALSE;

	if (vid_raw == DELL_VID) {
		*version_offset = (guint16)DOCK_RTSHUB_GEN1_VERSION_OFFSET;
		*pid_offset = (guint16)DOCK_RTSHUB_GEN1_PID_OFFSET;
		return TRUE;
	}

	*version_offset = (guint16)DOCK_RTSHUB_GEN2_VERSION_OFFSET;
	return TRUE;
}
static gboolean
fu_dell_kestrel_rtshub_firmware_parse(FuFirmware *firmware,
				      GInputStream *stream,
				      FwupdInstallFlags flags,
				      GError **error)
{
	FuDellKestrelRtshubFirmware *self = FU_DELL_KESTREL_RTSHUB_FIRMWARE(firmware);
	guint16 version_raw = 0;
	guint16 version_offset = 0;
	guint16 pid_raw = 0;
	guint16 pid_offset = 0;

	/* match vid first */
	if (!fu_dell_kestrel_rtshub_firmware_set_offset(stream,
							&version_offset,
							&pid_offset,
							error))
		return FALSE;

	/* version */
	if (!fu_input_stream_read_u16(stream, version_offset, &version_raw, G_BIG_ENDIAN, error))
		return FALSE;

	fu_firmware_set_version_raw(firmware, version_raw);

	/* pid */
	if (pid_offset != 0) {
		if (!fu_input_stream_read_u16(stream, pid_offset, &pid_raw, G_BIG_ENDIAN, error))
			return FALSE;
		self->pid = pid_raw;
	}
	return TRUE;
}

static void
fu_dell_kestrel_rtshub_firmware_init(FuDellKestrelRtshubFirmware *self)
{
	fu_firmware_set_version_format(FU_FIRMWARE(self), FWUPD_VERSION_FORMAT_PAIR);
}

static void
fu_dell_kestrel_rtshub_firmware_class_init(FuDellKestrelRtshubFirmwareClass *klass)
{
	FuFirmwareClass *firmware_class = FU_FIRMWARE_CLASS(klass);
	firmware_class->parse = fu_dell_kestrel_rtshub_firmware_parse;
	firmware_class->export = fu_dell_kestrel_rtshub_firmware_export;
	firmware_class->convert_version = fu_dell_kestrel_rtshub_firmware_convert_version;
}
