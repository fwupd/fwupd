/*
 * Copyright 2024 Dell Technologies
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later OR MIT
 */

#include "config.h"

#include "fu-dell-dock2-rtshub-firmware.h"

#define DOCK_RTSHUB_GEN2_VERSION_OFFSET 0x7F52
#define DOCK_RTSHUB_GEN1_VERSION_OFFSET 0x7FA6
#define DOCK_RTSHUB_GEN1_VID_OFFSET	0x7FA8
#define DOCK_RTSHUB_GEN1_PID_OFFSET	0x7FAA

#define DELL_VID 0x413C

struct _FuDellDock2RtshubFirmware {
	FuFirmwareClass parent_instance;
	guint16 pid;
};

G_DEFINE_TYPE(FuDellDock2RtshubFirmware, fu_dell_dock2_rtshub_firmware, FU_TYPE_FIRMWARE)

static void
fu_dell_dock2_rtshub_firmware_export(FuFirmware *firmware,
				     FuFirmwareExportFlags flags,
				     XbBuilderNode *bn)
{
	FuDellDock2RtshubFirmware *self = FU_DELL_DOCK2_RTSHUB_FIRMWARE(firmware);
	fu_xmlb_builder_insert_kx(bn, "product_id", self->pid);
}

static gboolean
fu_dell_dock2_rtshub_firmware_set_offset(GInputStream *stream,
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
fu_dell_dock2_rtshub_firmware_parse(FuFirmware *firmware,
				    GInputStream *stream,
				    gsize offset,
				    FwupdInstallFlags flags,
				    GError **error)
{
	FuDellDock2RtshubFirmware *self = FU_DELL_DOCK2_RTSHUB_FIRMWARE(firmware);
	g_autofree gchar *version_str = NULL;
	guint16 version_raw = 0;
	guint16 version_offset = 0;
	guint16 pid_raw = 0;
	guint16 pid_offset = 0;

	/* match vid first */
	if (!fu_dell_dock2_rtshub_firmware_set_offset(stream, &version_offset, &pid_offset, error))
		return FALSE;

	/* version */
	if (!fu_input_stream_read_u16(stream, version_offset, &version_raw, G_BIG_ENDIAN, error))
		return FALSE;

	version_str = fu_version_from_uint16(version_raw, FWUPD_VERSION_FORMAT_BCD);
	fu_firmware_set_version_raw(firmware, version_raw);
	fu_firmware_set_version(firmware, version_str);

	/* pid */
	if (pid_offset != 0) {
		if (!fu_input_stream_read_u16(stream, pid_offset, &pid_raw, G_BIG_ENDIAN, error))
			return FALSE;
		self->pid = pid_raw;
	}
	return TRUE;
}

static void
fu_dell_dock2_rtshub_firmware_init(FuDellDock2RtshubFirmware *self)
{
	self->pid = 0;
}

static void
fu_dell_dock2_rtshub_firmware_class_init(FuDellDock2RtshubFirmwareClass *klass)
{
	FuFirmwareClass *firmware_class = FU_FIRMWARE_CLASS(klass);
	firmware_class->parse = fu_dell_dock2_rtshub_firmware_parse;
	firmware_class->export = fu_dell_dock2_rtshub_firmware_export;
}
