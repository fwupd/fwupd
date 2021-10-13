/*
 * Copyright (C) 2021 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include "fu-lenovo-dock-firmware.h"

struct _FuLenovoDockFirmware {
	FuFirmware parent_instance;
};

G_DEFINE_TYPE(FuLenovoDockFirmware, fu_lenovo_dock_firmware, FU_TYPE_FIRMWARE)

/* revX is ‘/’ */
typedef struct __attribute__((packed)) {
	guint8 tag[2]; /* '5A' */
	guint8 rev1;
	guint8 ver[4]; /* ‘E104’ */
	guint8 rev2;
	guint8 date[10]; /* ‘2020/10/08’ */
	guint8 rev3;
	guint8 tag1[2]; /* ‘UG' */
	guint8 rev4;
	guint8 vid[4]; /* ’17EF’ */
	guint8 rev5;
	guint8 pid[4]; /* ‘30B4’ */
	guint8 rev6;
	guint8 file_cnt[4]; /* ‘00EF’ */
	guint8 rev7;
} IspLabel;

static gboolean
fu_lenovo_dock_firmware_parse(FuFirmware *firmware,
			      GBytes *fw,
			      guint64 addr_start,
			      guint64 addr_end,
			      FwupdInstallFlags flags,
			      GError **error)
{
	gsize offset = 0;
	gsize bufsz = 0;
	const guint8 *buf = g_bytes_get_data(fw, &bufsz);

	/* add each image to the firmware with the appropriate IDX set */
	while (offset < g_bytes_get_size(fw)) {
		guint8 tag = 0;
		guint16 fwsz = 0;
		gchar verbuf[5] = {0};
		gchar tag1buf[3] = {0};
		g_autoptr(FuFirmware) img = fu_firmware_new();
		g_autoptr(GBytes) blob_img = NULL;

		/* magic header byte */
		if (!fu_firmware_strparse_uint8_safe((const gchar *)buf,
						     bufsz,
						     offset + G_STRUCT_OFFSET(IspLabel, tag),
						     &tag,
						     error))
			return FALSE;
		if (tag != 0x5A) {
			g_set_error(error,
				    G_IO_ERROR,
				    G_IO_ERROR_NOT_SUPPORTED,
				    "got tag 0x%02x, expected 0x5A",
				    tag);
			return FALSE;
		}

		/* version */
		if (!fu_memcpy_safe((guint8 *)verbuf,
				    sizeof(verbuf),
				    0x0,
				    buf,
				    bufsz,
				    G_STRUCT_OFFSET(IspLabel, ver),
				    4,
				    error))
			return FALSE;
		fu_firmware_set_version(img, verbuf);

		/* tag1 */
		if (!fu_memcpy_safe((guint8 *)tag1buf,
				    sizeof(tag1buf),
				    0x0,
				    buf,
				    bufsz,
				    G_STRUCT_OFFSET(IspLabel, tag1),
				    2,
				    error))
			return FALSE;
		fu_firmware_set_id(img, tag1buf);

		/* file size */
		if (!fu_firmware_strparse_uint16_safe((const gchar *)buf,
						      bufsz,
						      offset + G_STRUCT_OFFSET(IspLabel, file_cnt),
						      &fwsz,
						      error))
			return FALSE;

		/* get file data */
		offset += sizeof(IspLabel);
		blob_img = fu_common_bytes_new_offset(fw, offset, fwsz, error);
		if (blob_img == NULL)
			return FALSE;
		fu_firmware_set_bytes(img, blob_img);

		/* done */
		fu_firmware_add_image(firmware, img);
		offset += fwsz;
	}

	/* success */
	return TRUE;
}

static void
fu_lenovo_dock_firmware_init(FuLenovoDockFirmware *self)
{
}

static void
fu_lenovo_dock_firmware_class_init(FuLenovoDockFirmwareClass *klass)
{
	FuFirmwareClass *klass_firmware = FU_FIRMWARE_CLASS(klass);
	klass_firmware->parse = fu_lenovo_dock_firmware_parse;
}

FuFirmware *
fu_lenovo_dock_firmware_new(void)
{
	return FU_FIRMWARE(g_object_new(FU_TYPE_LENOVO_DOCK_FIRMWARE, NULL));
}
