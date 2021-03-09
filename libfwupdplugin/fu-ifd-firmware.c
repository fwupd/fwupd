/*
 * Copyright (C) 2021 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#define G_LOG_DOMAIN				"FuFirmware"

#include "config.h"

#include "fu-common.h"
#include "fu-ifd-common.h"
#include "fu-ifd-firmware.h"
#include "fu-ifd-image.h"

/**
 * SECTION:fu-ifd-firmware
 * @short_description: IFD firmware image
 *
 * An object that represents a Intel Flash Descriptor.
 *
 * See also: #FuFirmware
 */

typedef struct {
	gboolean		 is_skylake;
	guint32			 descriptor_map0;
	guint32			 descriptor_map1;
	guint32			 descriptor_map2;
	guint8			 num_regions;
	guint8			 num_components;
	guint32			 flash_region_base_addr;
	guint32			 flash_component_base_addr;
	guint32			 flash_master_base_addr;
	guint32			 flash_master[4];	/* indexed from 1, ignore [0] */
	guint32			 flash_ich_strap_base_addr;
	guint32			 flash_mch_strap_base_addr;
	guint32			 components_rcd;
	guint32			 illegal_jedec;
	guint32			 illegal_jedec1;
	guint32			*flash_descriptor_regs;
} FuIfdFirmwarePrivate;

G_DEFINE_TYPE_WITH_PRIVATE (FuIfdFirmware, fu_ifd_firmware, FU_TYPE_FIRMWARE)
#define GET_PRIVATE(o) (fu_ifd_firmware_get_instance_private (o))

#define FU_IFD_SIZE				0x1000
#define FU_IFD_SIGNATURE			0x0FF0A55A

#define FU_IFD_FDBAR_RESERVED			0x0000
#define FU_IFD_FDBAR_SIGNATURE			0x0010
#define FU_IFD_FDBAR_DESCRIPTOR_MAP0		0x0014
#define FU_IFD_FDBAR_DESCRIPTOR_MAP1		0x0018
#define FU_IFD_FDBAR_DESCRIPTOR_MAP2		0x001C
#define FU_IFD_FDBAR_FLASH_UPPER_MAP1		0x0EFC
#define FU_IFD_FDBAR_OEM_SECTION		0x0F00

#define FU_IFD_FCBA_FLCOMP			0x0000
#define FU_IFD_FCBA_FLILL			0x0004
#define FU_IFD_FCBA_FLILL1			0x0008

#define FU_IFD_FREG_BASE(freg)			(((freg) << 12) & 0x07FFF000)
#define FU_IFD_FREG_LIMIT(freg)			((((freg) >> 4) & 0x07FFF000) | 0x00000FFF)

static void
fu_ifd_firmware_to_string (FuFirmware *firmware, guint idt, GString *str)
{
	FuIfdFirmware *self = FU_IFD_FIRMWARE (firmware);
	FuIfdFirmwarePrivate *priv = GET_PRIVATE (self);
	fu_common_string_append_kx (str, idt, "DescriptorMap0",
				    priv->descriptor_map0);
	fu_common_string_append_kx (str, idt, "DescriptorMap1",
				    priv->descriptor_map1);
	fu_common_string_append_kx (str, idt, "DescriptorMap2",
				    priv->descriptor_map2);
	fu_common_string_append_ku (str, idt, "NumRegions",
				    priv->num_regions);
	fu_common_string_append_ku (str, idt, "NumComponents",
				    priv->num_components + 1);
	fu_common_string_append_kx (str, idt, "FlashRegionBaseAddr",
				    priv->flash_region_base_addr);
	fu_common_string_append_kx (str, idt, "FlashComponentBaseAddr",
				    priv->flash_component_base_addr);
	fu_common_string_append_kx (str, idt, "FlashMasterBaseAddr",
				    priv->flash_master_base_addr);
	for (guint i = 1; i < 3; i++) {
		g_autofree gchar *title = g_strdup_printf ("FlashMaster%x", i + 1);
		fu_common_string_append_kx (str, idt, title,
					    priv->flash_master[i]);
	}
	fu_common_string_append_kx (str, idt, "FlashIchStrapBaseAddr",
				    priv->flash_ich_strap_base_addr);
	fu_common_string_append_kx (str, idt, "FlashMchStrapBaseAddr",
				    priv->flash_mch_strap_base_addr);
	fu_common_string_append_kx (str, idt, "ComponentsRcd",
				    priv->components_rcd);
	fu_common_string_append_kx (str, idt, "IllegalJedec", priv->illegal_jedec);
	fu_common_string_append_kx (str, idt, "IllegalJedec1", priv->illegal_jedec1);
	if (priv->flash_descriptor_regs != NULL) {
		for (guint i = 0; i < priv->num_regions; i++) {
			g_autofree gchar *title = g_strdup_printf ("FlashDescriptorReg%x", i);
			fu_common_string_append_kx (str, idt, title, priv->flash_descriptor_regs[i]);
		}
	}
}

static gboolean
fu_ifd_firmware_parse (FuFirmware *firmware,
		       GBytes *fw,
		       guint64 addr_start,
		       guint64 addr_end,
		       FwupdInstallFlags flags,
		       GError **error)
{
	FuIfdFirmware *self = FU_IFD_FIRMWARE (firmware);
	FuIfdFirmwarePrivate *priv = GET_PRIVATE (self);
	gsize bufsz = 0;
	guint32 sig;
	const guint8 *buf = g_bytes_get_data (fw, &bufsz);

	/* check size */
	if (bufsz < FU_IFD_SIZE) {
		g_set_error (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_INTERNAL,
			     "file is too small, expected bufsz >= 0x%x",
			     (guint) FU_IFD_SIZE);
		return FALSE;
	}

	/* check reserved section */
	for (guint i = 0; i < 0x10; i++) {
		if (buf[FU_IFD_FDBAR_RESERVED + i] != 0xff) {
			g_set_error (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_INTERNAL,
				     "reserved section invalid @0x%x",
				     FU_IFD_FDBAR_RESERVED + i);
			return FALSE;
		}
	}

	/* check signature */
	sig = fu_common_read_uint32 (buf + FU_IFD_FDBAR_SIGNATURE, G_LITTLE_ENDIAN);
	if (sig != FU_IFD_SIGNATURE) {
		g_set_error (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_INTERNAL,
			     "signature invalid, got 0x%x, expected 0x%x",
			     sig, (guint) FU_IFD_SIGNATURE);
		return FALSE;
	}

	/* descriptor registers */
	priv->descriptor_map0 = fu_common_read_uint32 (buf + FU_IFD_FDBAR_DESCRIPTOR_MAP0,
						       G_LITTLE_ENDIAN);
	priv->num_regions = (priv->descriptor_map0 >> 24) & 0b111;
	if (priv->num_regions == 0)
		priv->num_regions = 10;
	priv->num_components = (priv->descriptor_map0 >> 8) & 0b11;
	priv->flash_component_base_addr = (priv->descriptor_map0 <<  4) & 0x00000FF0;
	priv->flash_region_base_addr = (priv->descriptor_map0 >> 12) & 0x00000FF0;
	priv->descriptor_map1 = fu_common_read_uint32 (buf + FU_IFD_FDBAR_DESCRIPTOR_MAP1,
						       G_LITTLE_ENDIAN);
	priv->flash_master_base_addr = (priv->descriptor_map1 <<  4) & 0x00000FF0;
	priv->flash_ich_strap_base_addr = (priv->descriptor_map1 >> 12) & 0x00000FF0;
	priv->descriptor_map2 = fu_common_read_uint32 (buf + FU_IFD_FDBAR_DESCRIPTOR_MAP2,
						       G_LITTLE_ENDIAN);
	priv->flash_mch_strap_base_addr = (priv->descriptor_map2 <<  4) & 0x00000FF0;

	/* FCBA */
	if (!fu_common_read_uint32_safe (buf, bufsz,
					 priv->flash_component_base_addr + FU_IFD_FCBA_FLCOMP,
					 &priv->components_rcd, G_LITTLE_ENDIAN, error))
		return FALSE;
	if (!fu_common_read_uint32_safe (buf, bufsz,
					 priv->flash_component_base_addr + FU_IFD_FCBA_FLILL,
					 &priv->illegal_jedec, G_LITTLE_ENDIAN, error))
		return FALSE;
	if (!fu_common_read_uint32_safe (buf, bufsz,
					 priv->flash_component_base_addr + FU_IFD_FCBA_FLILL1,
					 &priv->illegal_jedec1, G_LITTLE_ENDIAN, error))
		return FALSE;

	/* FMBA */
	if (!fu_common_read_uint32_safe (buf, bufsz,
					 priv->flash_master_base_addr + 0x0,
					 &priv->flash_master[1], G_LITTLE_ENDIAN, error))
		return FALSE;
	if (!fu_common_read_uint32_safe (buf, bufsz,
					 priv->flash_master_base_addr + 0x4,
					 &priv->flash_master[2], G_LITTLE_ENDIAN, error))
		return FALSE;
	if (!fu_common_read_uint32_safe (buf, bufsz,
					 priv->flash_master_base_addr + 0x8,
					 &priv->flash_master[3], G_LITTLE_ENDIAN, error))
		return FALSE;

	/* FRBA */
	priv->flash_descriptor_regs = g_new0 (guint32, priv->num_regions);
	for (guint i = 0; i < priv->num_regions; i++) {
		if (!fu_common_read_uint32_safe (buf, bufsz,
						 priv->flash_region_base_addr + (i * sizeof(guint32)),
						 &priv->flash_descriptor_regs[i], G_LITTLE_ENDIAN, error))
			return FALSE;
	}
	for (guint i = 0; i < priv->num_regions; i++) {
		const gchar *freg_str = fu_ifd_region_to_string (i);
		guint32 freg_base = FU_IFD_FREG_BASE(priv->flash_descriptor_regs[i]);
		guint32 freg_limt = FU_IFD_FREG_LIMIT(priv->flash_descriptor_regs[i]);
		guint32 freg_size = freg_limt - freg_base;
		g_autoptr(FuFirmware) img = NULL;
		g_autoptr(GBytes) contents = NULL;
		guint8 bit_r = 0;
		guint8 bit_w = 0;

		/* invalid */
		if (freg_base > freg_limt)
			continue;

		/* create image */
		g_debug ("freg %s 0x%04x -> 0x%04x", freg_str, freg_base, freg_limt);
		contents = fu_common_bytes_new_offset (fw, freg_base, freg_size, error);
		if (contents == NULL)
			return FALSE;
		img = fu_ifd_image_new ();
		fu_firmware_set_bytes (img, contents);
		fu_firmware_set_addr (img, freg_base);
		fu_firmware_set_idx (img, i);
		if (freg_str != NULL)
			fu_firmware_set_id (img, freg_str);
		fu_firmware_add_image (firmware, img);

		/* is writable by anything other than the region itself */
		if (priv->is_skylake) {
			for (FuIfdRegion r = 1; r <= 3; r++) {
				bit_r = (priv->flash_master[r] >> (i + 8)) & 0b1;
				bit_w = (priv->flash_master[r] >> (i + 20)) & 0b1;
				fu_ifd_image_set_access (FU_IFD_IMAGE (img), r,
							 (bit_r ? FU_IFD_ACCESS_READ : FU_IFD_ACCESS_NONE) |
							 (bit_w ? FU_IFD_ACCESS_WRITE : FU_IFD_ACCESS_NONE));
			}
		} else {
			if (i == FU_IFD_REGION_DESC) {
				bit_r = 16;
				bit_w = 24;
			} else if (i == FU_IFD_REGION_BIOS) {
				bit_r = 17;
				bit_w = 25;
			} else if (i == FU_IFD_REGION_ME) {
				bit_r = 18;
				bit_w = 26;
			} else if (i == FU_IFD_REGION_GBE) {
				bit_r = 19;
				bit_w = 27;
			}
			for (FuIfdRegion r = 1; r <= 3 && bit_r != 0; r++) {
				fu_ifd_image_set_access (FU_IFD_IMAGE (img), r,
							 ((priv->flash_master[r] >> bit_r) & 0b1 ?
							  FU_IFD_ACCESS_READ : FU_IFD_ACCESS_NONE) |
							 ((priv->flash_master[r] >> bit_w) & 0b1 ?
							  FU_IFD_ACCESS_WRITE : FU_IFD_ACCESS_NONE));
			}
		}
	}

	/* success */
	return TRUE;
}

/**
 * fu_ifd_firmware_check_jedec_cmd:
 * @self: a #FuIfdFirmware
 * @cmd: a JEDEC command, e.g. 0x42 for "whole chip erase"
 *
 * Checks a JEDEC command to see if it has been put on the "illegal_jedec" list.
 *
 * Return value: %TRUE if the command is allowed
 *
 * Since: 1.6.0
 **/
gboolean
fu_ifd_firmware_check_jedec_cmd (FuIfdFirmware *self, guint8 cmd)
{
	FuIfdFirmwarePrivate *priv = GET_PRIVATE (self);
	for (guint j = 0; j < 32; j += 8) {
		if (((priv->illegal_jedec >> j) & 0xff) == cmd)
			return FALSE;
		if (((priv->illegal_jedec1 >> j) & 0xff) == cmd)
			return FALSE;
	}
	return TRUE;
}

static GBytes *
fu_ifd_firmware_write (FuFirmware *firmware, GError **error)
{
	FuIfdFirmware *self = FU_IFD_FIRMWARE (firmware);
	FuIfdFirmwarePrivate *priv = GET_PRIVATE (self);
	gsize bufsz_max = FU_IFD_SIZE;
	g_autoptr(GByteArray) buf = g_byte_array_new ();
	g_autoptr(GPtrArray) images = fu_firmware_get_images (firmware);

	/* get total size */
	for (guint i = 0; i < images->len; i++) {
		FuFirmware *img = g_ptr_array_index (images, i);
		g_autoptr(GBytes) blob = NULL;
		guint32 freg_base = fu_firmware_get_addr (img);
		blob = fu_firmware_get_bytes (img, error);
		if (blob == NULL)
			return NULL;
		bufsz_max = MAX(freg_base + MAX(g_bytes_get_size (blob), 0x1000), bufsz_max);
	}
	fu_byte_array_set_size (buf, bufsz_max);

	/* reserved */
	for (guint i = 0; i < 0x10; i++)
		buf->data[FU_IFD_FDBAR_RESERVED+i] = 0xff;

	/* signature */
	fu_common_write_uint32 (buf->data + FU_IFD_FDBAR_SIGNATURE,
				FU_IFD_SIGNATURE, G_LITTLE_ENDIAN);

	/* descriptor map */
	fu_common_write_uint32 (buf->data + FU_IFD_FDBAR_DESCRIPTOR_MAP0,
				priv->descriptor_map0, G_LITTLE_ENDIAN);
	fu_common_write_uint32 (buf->data + FU_IFD_FDBAR_DESCRIPTOR_MAP1,
				priv->descriptor_map1, G_LITTLE_ENDIAN);
	fu_common_write_uint32 (buf->data + FU_IFD_FDBAR_DESCRIPTOR_MAP2,
				priv->descriptor_map2, G_LITTLE_ENDIAN);

	/* FCBA */
	if (!fu_common_write_uint32_safe (buf->data, buf->len,
					  priv->flash_component_base_addr + FU_IFD_FCBA_FLCOMP,
					  priv->components_rcd, G_LITTLE_ENDIAN, error))
		return NULL;
	if (!fu_common_write_uint32_safe (buf->data, buf->len,
					  priv->flash_component_base_addr + FU_IFD_FCBA_FLILL,
					  priv->illegal_jedec, G_LITTLE_ENDIAN, error))
		return NULL;
	if (!fu_common_write_uint32_safe (buf->data, buf->len,
					  priv->flash_component_base_addr + FU_IFD_FCBA_FLILL1,
					  priv->illegal_jedec1, G_LITTLE_ENDIAN, error))
		return NULL;

	/* FRBA */
	for (guint i = 0; i < priv->num_regions; i++) {
		guint32 freg_base = 0x7FFF000;
		guint32 freg_limt = 0x0;
		guint32 flreg;
		g_autoptr(FuFirmware) img = fu_firmware_get_image_by_idx (firmware, i, NULL);
		if (img != NULL) {
			g_autoptr(GBytes) blob = fu_firmware_get_bytes (img, error);
			if (blob == NULL)
				return NULL;
			freg_base = fu_firmware_get_addr (img);
			freg_limt = freg_base + g_bytes_get_size (blob);
		}
		flreg = ((freg_limt << 4) & 0xFFFF0000) | (freg_base >> 12);
		g_debug ("freg 0x%04x -> 0x%04x = 0x%08x", freg_base, freg_limt, flreg);
		if (!fu_common_write_uint32_safe (buf->data, buf->len,
						  priv->flash_region_base_addr + (i * sizeof(guint32)),
						  flreg, G_LITTLE_ENDIAN, error))
			return NULL;
	}

	/* write images at correct offsets */
	for (guint i = 0; i < images->len; i++) {
		FuFirmware *img = g_ptr_array_index (images, i);
		g_autoptr(GBytes) blob = fu_firmware_get_bytes (img, error);
		if (blob == NULL)
			return NULL;
		if (!fu_memcpy_safe (buf->data, buf->len, fu_firmware_get_addr (img),
				     g_bytes_get_data (blob, NULL), g_bytes_get_size (blob), 0x0,
				     g_bytes_get_size (blob), error))
			return NULL;
	}

	/* success */
	return g_byte_array_free_to_bytes (g_steal_pointer (&buf));
}

static gboolean
fu_ifd_firmware_build (FuFirmware *firmware, XbNode *n, GError **error)
{
	FuIfdFirmware *self = FU_IFD_FIRMWARE (firmware);
	FuIfdFirmwarePrivate *priv = GET_PRIVATE (self);
	guint64 tmp;

	/* optional properties */
	tmp = xb_node_query_text_as_uint (n, "descriptor_map0", NULL);
	if (tmp != G_MAXUINT64 && tmp <= G_MAXUINT32)
		priv->descriptor_map0 = tmp;
	tmp = xb_node_query_text_as_uint (n, "descriptor_map1", NULL);
	if (tmp != G_MAXUINT64 && tmp <= G_MAXUINT32)
		priv->descriptor_map1 = tmp;
	tmp = xb_node_query_text_as_uint (n, "descriptor_map2", NULL);
	if (tmp != G_MAXUINT64 && tmp <= G_MAXUINT32)
		priv->descriptor_map2 = tmp;
	tmp = xb_node_query_text_as_uint (n, "components_rcd", NULL);
	if (tmp != G_MAXUINT64 && tmp <= G_MAXUINT32)
		priv->components_rcd = tmp;
	tmp = xb_node_query_text_as_uint (n, "illegal_jedec", NULL);
	if (tmp != G_MAXUINT64) {
		priv->illegal_jedec = tmp & 0xFFFFFFFF;
		priv->illegal_jedec1 = tmp >> 32;
	}

	/* success */
	return TRUE;
}

static void
fu_ifd_firmware_init (FuIfdFirmware *self)
{
	FuIfdFirmwarePrivate *priv = GET_PRIVATE (self);

	/* some good defaults */
	priv->is_skylake = TRUE;
	priv->num_regions = 10;
	priv->flash_region_base_addr = 0x40;
	priv->flash_component_base_addr = 0x30;
	priv->flash_master_base_addr = 0x80;
	priv->flash_master[1] = 0x00A00F00;
	priv->flash_master[2] = 0x00400D00;
	priv->flash_master[3] = 0x00800900;
	priv->flash_ich_strap_base_addr = 0x100;
	priv->flash_mch_strap_base_addr = 0x300;
}

static void
fu_ifd_firmware_finalize (GObject *object)
{
	FuIfdFirmware *self = FU_IFD_FIRMWARE (object);
	FuIfdFirmwarePrivate *priv = GET_PRIVATE (self);
	g_free (priv->flash_descriptor_regs);
	G_OBJECT_CLASS (fu_ifd_firmware_parent_class)->finalize (object);
}


static void
fu_ifd_firmware_class_init (FuIfdFirmwareClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	FuFirmwareClass *klass_firmware = FU_FIRMWARE_CLASS (klass);
	object_class->finalize = fu_ifd_firmware_finalize;
	klass_firmware->to_string = fu_ifd_firmware_to_string;
	klass_firmware->parse = fu_ifd_firmware_parse;
	klass_firmware->write = fu_ifd_firmware_write;
	klass_firmware->build = fu_ifd_firmware_build;
}

/**
 * fu_ifd_firmware_new:
 *
 * Creates a new #FuFirmware of sub type Ifd
 *
 * Since: 1.6.0
 **/
FuFirmware *
fu_ifd_firmware_new (void)
{
	return FU_FIRMWARE (g_object_new (FU_TYPE_IFD_FIRMWARE, NULL));
}
