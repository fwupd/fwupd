/*
 * Copyright (C) 2020 Benson Leung <bleung@chromium.org>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include "fu-byte-array.h"
#include "fu-bytes.h"
#include "fu-common.h"
#include "fu-fmap-firmware.h"
#include "fu-mem.h"

/**
 * FuFmapFirmware:
 *
 * A FMAP firmware image.
 *
 * See also: [class@FuFirmware]
 */

#define FMAP_SIGNATURE "__FMAP__"
#define FMAP_AREANAME  "FMAP"

G_DEFINE_TYPE(FuFmapFirmware, fu_fmap_firmware, FU_TYPE_FIRMWARE)

static gboolean
fu_fmap_firmware_parse(FuFirmware *firmware,
		       GBytes *fw,
		       gsize offset,
		       FwupdInstallFlags flags,
		       GError **error)
{
	FuFmapFirmwareClass *klass_firmware = FU_FMAP_FIRMWARE_GET_CLASS(firmware);
	gsize bufsz;
	const guint8 *buf = g_bytes_get_data(fw, &bufsz);
	FuFmap fmap;

	/* corrupt */
	if (g_bytes_get_size(fw) < sizeof(FuFmap)) {
		g_set_error_literal(error,
				    G_IO_ERROR,
				    G_IO_ERROR_INVALID_DATA,
				    "firmware too small for fmap");
		return FALSE;
	}

	/* only search for the fmap signature if not fuzzing */
	if ((flags & FWUPD_INSTALL_FLAG_NO_SEARCH) == 0) {
		if (!fu_memmem_safe(buf, bufsz, (const guint8 *)FMAP_SIGNATURE, 8, &offset, error))
			return FALSE;
		fu_firmware_set_offset(firmware, offset);
	}

	/* load header */
	if (!fu_memcpy_safe((guint8 *)&fmap,
			    sizeof(fmap),
			    0x0, /* dst */
			    buf,
			    bufsz,
			    offset, /* src */
			    sizeof(fmap),
			    error))
		return FALSE;
	fu_firmware_set_addr(firmware, GUINT64_FROM_LE(fmap.base));

	if (GUINT32_FROM_LE(fmap.size) != bufsz) {
		g_set_error(error,
			    G_IO_ERROR,
			    G_IO_ERROR_INVALID_DATA,
			    "file size incorrect, expected 0x%04x got 0x%04x",
			    (guint)fmap.size,
			    (guint)bufsz);
		return FALSE;
	}
	if (GUINT16_FROM_LE(fmap.nareas) < 1) {
		g_set_error(error,
			    G_IO_ERROR,
			    G_IO_ERROR_INVALID_DATA,
			    "number of areas too small, got %" G_GUINT16_FORMAT,
			    GUINT16_FROM_LE(fmap.nareas));
		return FALSE;
	}
	offset += sizeof(fmap);

	for (gsize i = 0; i < GUINT16_FROM_LE(fmap.nareas); i++) {
		FuFmapArea area;
		g_autoptr(FuFirmware) img = NULL;
		g_autoptr(GBytes) bytes = NULL;
		g_autofree gchar *area_name = NULL;

		/* load area */
		if (!fu_memcpy_safe((guint8 *)&area,
				    sizeof(area),
				    0x0, /* dst */
				    buf,
				    bufsz,
				    offset, /* src */
				    sizeof(area),
				    error))
			return FALSE;

		/* skip */
		if (area.size == 0)
			continue;

		bytes = fu_bytes_new_offset(fw,
					    (gsize)GUINT32_FROM_LE(area.offset),
					    (gsize)GUINT32_FROM_LE(area.size),
					    error);
		if (bytes == NULL)
			return FALSE;
		area_name = g_strndup((const gchar *)area.name, FU_FMAP_FIRMWARE_STRLEN);
		img = fu_firmware_new_from_bytes(bytes);
		fu_firmware_set_id(img, area_name);
		fu_firmware_set_idx(img, i + 1);
		fu_firmware_set_addr(img, GUINT32_FROM_LE(area.offset));
		fu_firmware_add_image(firmware, img);

		if (g_strcmp0(area_name, FMAP_AREANAME) == 0) {
			g_autofree gchar *version = NULL;
			version = g_strdup_printf("%d.%d", fmap.ver_major, fmap.ver_minor);
			fu_firmware_set_version(img, version);
		}
		offset += sizeof(area);
	}

	/* subclassed */
	if (klass_firmware->parse != NULL) {
		if (!klass_firmware->parse(firmware, fw, offset, flags, error))
			return FALSE;
	}

	/* success */
	return TRUE;
}

static GBytes *
fu_fmap_firmware_write(FuFirmware *firmware, GError **error)
{
	gsize total_sz;
	gsize offset;
	g_autoptr(GPtrArray) images = fu_firmware_get_images(firmware);
	g_autoptr(GByteArray) buf = g_byte_array_new();
	FuFmap hdr = {
	    .signature = {FMAP_SIGNATURE},
	    .ver_major = 0x1,
	    .ver_minor = 0x1,
	    .base = GUINT64_TO_LE(fu_firmware_get_addr(firmware)),
	    .size = 0x0,
	    .name = "",
	    .nareas = GUINT16_TO_LE(images->len),
	};

	/* pad to offset */
	if (fu_firmware_get_offset(firmware) > 0)
		fu_byte_array_set_size(buf, fu_firmware_get_offset(firmware), 0x00);

	/* add header */
	total_sz = offset = sizeof(hdr) + (sizeof(FuFmapArea) * images->len);
	for (guint i = 0; i < images->len; i++) {
		FuFirmware *img = g_ptr_array_index(images, i);
		g_autoptr(GBytes) fw = fu_firmware_get_bytes_with_patches(img, error);
		if (fw == NULL)
			return NULL;
		total_sz += g_bytes_get_size(fw);
	}
	hdr.size = GUINT32_TO_LE(fu_firmware_get_offset(firmware) + total_sz);
	g_byte_array_append(buf, (const guint8 *)&hdr, sizeof(hdr));

	/* add each area */
	for (guint i = 0; i < images->len; i++) {
		FuFirmware *img = g_ptr_array_index(images, i);
		const gchar *id = fu_firmware_get_id(img);
		g_autoptr(GBytes) fw = fu_firmware_get_bytes_with_patches(img, NULL);
		FuFmapArea area = {
		    .offset = GUINT32_TO_LE(fu_firmware_get_offset(firmware) + offset),
		    .size = GUINT32_TO_LE(g_bytes_get_size(fw)),
		    .name = {""},
		    .flags = 0x0,
		};
		if (id != NULL)
			strncpy((gchar *)area.name, id, sizeof(area.name) - 1);
		g_byte_array_append(buf, (const guint8 *)&area, sizeof(area));
		offset += g_bytes_get_size(fw);
	}

	/* add the images */
	for (guint i = 0; i < images->len; i++) {
		FuFirmware *img = g_ptr_array_index(images, i);
		g_autoptr(GBytes) fw = fu_firmware_get_bytes_with_patches(img, NULL);
		fu_byte_array_append_bytes(buf, fw);
	}

	/* success */
	return g_byte_array_free_to_bytes(g_steal_pointer(&buf));
}

static void
fu_fmap_firmware_init(FuFmapFirmware *self)
{
}

static void
fu_fmap_firmware_class_init(FuFmapFirmwareClass *klass)
{
	FuFirmwareClass *klass_firmware = FU_FIRMWARE_CLASS(klass);
	klass_firmware->parse = fu_fmap_firmware_parse;
	klass_firmware->write = fu_fmap_firmware_write;
}

/**
 * fu_fmap_firmware_new
 *
 * Creates a new #FuFirmware of sub type fmap
 *
 * Since: 1.5.0
 **/
FuFirmware *
fu_fmap_firmware_new(void)
{
	return FU_FIRMWARE(g_object_new(FU_TYPE_FMAP_FIRMWARE, NULL));
}
