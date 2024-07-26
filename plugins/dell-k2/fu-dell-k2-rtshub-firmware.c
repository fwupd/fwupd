/*
 * Copyright 2024 Dell Technologies
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later OR MIT
 */

#include "config.h"

#include "fu-dell-k2-common.h"
#include "fu-dell-k2-rtshub-firmware.h"

#define DOCK_RTSHUB_GEN2_VERSION_OFFSET 0x7F52
#define DOCK_RTSHUB_GEN1_VERSION_OFFSET 0x7FA6
#define DOCK_RTSHUB_GEN1_VID_OFFSET	0x7FA8
#define DOCK_RTSHUB_GEN1_PID_OFFSET	0x7FAA

struct _FuDellK2RtshubFirmware {
	FuFirmwareClass parent_instance;
	guint16 pid;
};

G_DEFINE_TYPE(FuDellK2RtshubFirmware, fu_dell_k2_rtshub_firmware, FU_TYPE_FIRMWARE)

static void
fu_dell_k2_rtshub_firmware_export(FuFirmware *firmware,
				  FuFirmwareExportFlags flags,
				  XbBuilderNode *bn)
{
	FuDellK2RtshubFirmware *self = FU_DELL_K2_RTSHUB_FIRMWARE(firmware);
	fu_xmlb_builder_insert_kx(bn, "product_id", self->pid);
}

static gboolean
fu_dell_k2_rtshub_firmware_set_offset(GBytes *fw,
				      guint16 *version_offset,
				      guint16 *pid_offset,
				      GError **error)
{
	guint16 vid_raw = 0;
	gsize bufsz = 0;
	const guint8 *buf = g_bytes_get_data(fw, &bufsz);

	if (!fu_memread_uint16_safe(buf,
				    bufsz,
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
fu_dell_k2_rtshub_firmware_parse(FuFirmware *firmware,
				 GBytes *fw,
				 gsize offset,
				 FwupdInstallFlags flags,
				 GError **error)
{
	FuDellK2RtshubFirmware *self = FU_DELL_K2_RTSHUB_FIRMWARE(firmware);
	guint16 version_raw = 0;
	guint16 version_offset = 0;
	guint16 pid_raw = 0;
	guint16 pid_offset = 0;
	gsize bufsz = 0;
	g_autofree gchar *version = NULL;
	const guint8 *buf = g_bytes_get_data(fw, &bufsz);

	/* match vid first */
	if (!fu_dell_k2_rtshub_firmware_set_offset(fw, &version_offset, &pid_offset, error))
		return FALSE;

	/* version */
	if (!fu_memread_uint16_safe(buf, bufsz, version_offset, &version_raw, G_BIG_ENDIAN, error))
		return FALSE;

	version = fu_version_from_uint32_hex(version_raw, FWUPD_VERSION_FORMAT_PAIR);
	fu_firmware_set_version(firmware, version);

	/* pid */
	if (pid_offset != 0) {
		if (!fu_memread_uint16_safe(buf, bufsz, pid_offset, &pid_raw, G_BIG_ENDIAN, error))
			return FALSE;
		self->pid = pid_raw;
	}
	return TRUE;
}

static void
fu_dell_k2_rtshub_firmware_init(FuDellK2RtshubFirmware *self)
{
	self->pid = 0;
}

static void
fu_dell_k2_rtshub_firmware_class_init(FuDellK2RtshubFirmwareClass *klass)
{
	FuFirmwareClass *firmware_class = FU_FIRMWARE_CLASS(klass);
	firmware_class->parse = fu_dell_k2_rtshub_firmware_parse;
	firmware_class->export = fu_dell_k2_rtshub_firmware_export;
}
