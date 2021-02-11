/*
 * Copyright (C) 2018 Evan Lojewski
 * Copyright (C) 2020 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include "fu-common.h"

#include "fu-bcm57xx-common.h"
#include "fu-bcm57xx-firmware.h"
#include "fu-bcm57xx-dict-image.h"
#include "fu-bcm57xx-stage1-image.h"
#include "fu-bcm57xx-stage2-image.h"

struct _FuBcm57xxFirmware {
	FuFirmware		 parent_instance;
	guint16			 vendor;
	guint16			 model;
	gboolean		 is_backup;
	guint32			 phys_addr;
	gsize			 source_size;
	guint8			 source_padchar;
};

G_DEFINE_TYPE (FuBcm57xxFirmware, fu_bcm57xx_firmware, FU_TYPE_FIRMWARE)

#define BCM_STAGE1_HEADER_MAGIC_BROADCOM	0x0E000E03
#define BCM_STAGE1_HEADER_MAGIC_MEKLORT		0x3C1D0800

#define BCM_APE_HEADER_MAGIC			0x1A4D4342

#define BCM_CODE_DIRECTORY_ADDR_APE		0x07

static void
fu_bcm57xx_firmware_to_string (FuFirmware *firmware, guint idt, GString *str)
{
	FuBcm57xxFirmware *self = FU_BCM57XX_FIRMWARE (firmware);
	fu_common_string_append_kx (str, idt, "Vendor", self->vendor);
	fu_common_string_append_kx (str, idt, "Model", self->model);
	fu_common_string_append_kb (str, idt, "IsBackup", self->is_backup);
	fu_common_string_append_kx (str, idt, "PhysAddr", self->phys_addr);
}

static gboolean
fu_bcm57xx_firmware_parse_header (FuBcm57xxFirmware *self, GBytes *fw, GError **error)
{
	gsize bufsz = 0x0;
	const guint8 *buf = g_bytes_get_data (fw, &bufsz);

	/* verify magic and CRC */
	if (!fu_bcm57xx_verify_magic (fw, 0x0, error))
		return FALSE;
	if (!fu_bcm57xx_verify_crc (fw, error))
		return FALSE;

	/* get address */
	return fu_common_read_uint32_safe (buf, bufsz, BCM_NVRAM_HEADER_PHYS_ADDR,
					   &self->phys_addr, G_BIG_ENDIAN, error);
}

static FuFirmwareImage *
fu_bcm57xx_firmware_parse_info (FuBcm57xxFirmware *self, GBytes *fw, GError **error)
{
	gsize bufsz = 0x0;
	guint32 mac_addr0 = 0;
	const guint8 *buf = g_bytes_get_data (fw, &bufsz);
	g_autoptr(FuFirmwareImage) img = fu_firmware_image_new (fw);

	/* if the MAC is set non-zero this is an actual backup rather than a container */
	if (!fu_common_read_uint32_safe (buf, bufsz, BCM_NVRAM_INFO_MAC_ADDR0,
					 &mac_addr0, G_BIG_ENDIAN, error))
		return NULL;
	self->is_backup = mac_addr0 != 0x0 && mac_addr0 != 0xffffffff;

	/* read vendor + model */
	if (!fu_common_read_uint16_safe (buf, bufsz, BCM_NVRAM_INFO_VENDOR,
					 &self->vendor, G_BIG_ENDIAN, error))
		return NULL;
	if (!fu_common_read_uint16_safe (buf, bufsz, BCM_NVRAM_INFO_DEVICE,
					 &self->model, G_BIG_ENDIAN, error))
		return NULL;

	/* success */
	fu_firmware_image_set_id (img, "info");
	return g_steal_pointer (&img);
}

static FuFirmwareImage *
fu_bcm57xx_firmware_parse_stage1 (FuBcm57xxFirmware *self,
				  GBytes *fw,
				  guint32 *out_stage1_sz,
				  FwupdInstallFlags flags,
				  GError **error)
{
	gsize bufsz = 0x0;
	guint32 stage1_wrds = 0;
	guint32 stage1_sz;
	guint32 stage1_off = 0;
	const guint8 *buf = g_bytes_get_data (fw, &bufsz);
	g_autoptr(FuFirmwareImage) img = fu_bcm57xx_stage1_image_new ();
	g_autoptr(GBytes) blob = NULL;

	if (!fu_common_read_uint32_safe (buf, bufsz,
					 BCM_NVRAM_HEADER_BASE + BCM_NVRAM_HEADER_SIZE_WRDS,
					 &stage1_wrds, G_BIG_ENDIAN, error))
		return NULL;
	if (!fu_common_read_uint32_safe (buf, bufsz,
					 BCM_NVRAM_HEADER_BASE + BCM_NVRAM_HEADER_OFFSET,
					 &stage1_off, G_BIG_ENDIAN, error))
		return NULL;
	stage1_sz = (stage1_wrds * sizeof(guint32));
	if (stage1_off != BCM_NVRAM_STAGE1_BASE) {
		g_set_error (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_NOT_SUPPORTED,
			     "stage1 offset invalid, got: 0x%x, expected 0x%x",
			     (guint) stage1_sz, (guint) BCM_NVRAM_STAGE1_BASE);
		return NULL;
	}
	if (stage1_off + stage1_sz > bufsz) {
		g_set_error (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_NOT_SUPPORTED,
			     "bigger than firmware, got: 0x%x @ 0x%x",
			     (guint) stage1_sz, (guint) stage1_off);
		return NULL;
	}

	/* verify CRC */
	blob = fu_common_bytes_new_offset (fw, stage1_off, stage1_sz, error);
	if (blob == NULL)
		return NULL;
	if (!fu_firmware_image_parse (img, blob, flags, error))
		return NULL;

	/* needed for stage2 */
	if (out_stage1_sz != NULL)
		*out_stage1_sz = stage1_sz;

	/* success */
	fu_firmware_image_set_id (img, "stage1");
	fu_firmware_image_set_offset (img, stage1_off);
	return g_steal_pointer (&img);
}

static FuFirmwareImage *
fu_bcm57xx_firmware_parse_stage2 (FuBcm57xxFirmware *self,
				  GBytes *fw,
				  guint32 stage1_sz,
				  FwupdInstallFlags flags,
				  GError **error)
{
	gsize bufsz = 0x0;
	const guint8 *buf = g_bytes_get_data (fw, &bufsz);
	guint32 stage2_off = 0;
	guint32 stage2_sz = 0;
	g_autoptr(FuFirmwareImage) img = fu_bcm57xx_stage2_image_new ();
	g_autoptr(GBytes) blob = NULL;

	stage2_off = BCM_NVRAM_STAGE1_BASE + stage1_sz;
	if (!fu_bcm57xx_verify_magic (fw, stage2_off, error))
		return NULL;
	if (!fu_common_read_uint32_safe (buf, bufsz, stage2_off + sizeof(guint32),
					 &stage2_sz, G_BIG_ENDIAN, error))
		return NULL;
	if (stage2_off + stage2_sz > bufsz) {
		g_set_error (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_NOT_SUPPORTED,
			     "bigger than firmware, got: 0x%x @ 0x%x",
			     (guint) stage2_sz, (guint) stage2_off);
		return NULL;
	}

	/* verify CRC */
	blob = fu_common_bytes_new_offset (fw, stage2_off + 0x8, stage2_sz, error);
	if (blob == NULL)
		return NULL;
	if (!fu_firmware_image_parse (img, blob, flags, error))
		return NULL;

	/* success */
	fu_firmware_image_set_id (img, "stage2");
	fu_firmware_image_set_offset (img, stage2_off);
	return g_steal_pointer (&img);
}

static gboolean
fu_bcm57xx_firmware_parse_dict (FuBcm57xxFirmware *self, GBytes *fw, guint idx,
				FwupdInstallFlags flags, GError **error)
{
	gsize bufsz = 0x0;
	guint32 dict_addr = 0x0;
	guint32 dict_info = 0x0;
	guint32 dict_off = 0x0;
	guint32 dict_sz;
	guint32 base = BCM_NVRAM_DIRECTORY_BASE + (idx * BCM_NVRAM_DIRECTORY_SZ);
	const guint8 *buf = g_bytes_get_data (fw, &bufsz);
	g_autoptr(FuFirmwareImage) img = fu_bcm57xx_dict_image_new ();
	g_autoptr(GBytes) blob = NULL;

	/* header */
	if (!fu_common_read_uint32_safe (buf, bufsz,
					 base + BCM_NVRAM_DIRECTORY_ADDR,
					 &dict_addr, G_BIG_ENDIAN, error))
		return FALSE;
	if (!fu_common_read_uint32_safe (buf, bufsz,
					 base + BCM_NVRAM_DIRECTORY_SIZE_WRDS,
					 &dict_info, G_BIG_ENDIAN, error))
		return FALSE;
	if (!fu_common_read_uint32_safe (buf, bufsz,
					 base + BCM_NVRAM_DIRECTORY_OFFSET,
					 &dict_off, G_BIG_ENDIAN, error))
		return FALSE;

	/* no dict stored */
	if (dict_addr == 0 && dict_info == 0 && dict_off == 0)
		return TRUE;

	dict_sz = (dict_info & 0x00FFFFFF) * sizeof(guint32);	/* implies that maximum size is 16 MB */
	fu_bcm57xx_dict_image_set_target (FU_BCM57XX_DICT_IMAGE (img), (dict_info & 0x0F000000) >> 24);
	fu_bcm57xx_dict_image_set_kind (FU_BCM57XX_DICT_IMAGE (img), (dict_info & 0xF0000000) >> 28);
	fu_firmware_image_set_addr (img, dict_addr);
	fu_firmware_image_set_offset (img, dict_off);
	fu_firmware_image_set_idx (img, 0x80 + idx);

	/* empty */
	if (dict_sz == 0) {
		blob = g_bytes_new (NULL, 0);
		fu_firmware_image_set_bytes (img, blob);
		fu_firmware_add_image (FU_FIRMWARE (self), img);
		return TRUE;
	}

	/* check against image size */
	if (dict_off + dict_sz > bufsz) {
		g_set_error (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_NOT_SUPPORTED,
			     "bigger than firmware, got: 0x%x @ 0x%x",
			     (guint) dict_sz, (guint) dict_off);
		return FALSE;
	}
	blob = fu_common_bytes_new_offset (fw, dict_off, dict_sz, error);
	if (blob == NULL)
		return FALSE;
	if (!fu_firmware_image_parse (img, blob, flags, error))
		return FALSE;

	/* success */
	fu_firmware_add_image (FU_FIRMWARE (self), img);
	return TRUE;
}

static gboolean
fu_bcm57xx_firmware_parse (FuFirmware *firmware,
			   GBytes *fw,
			   guint64 addr_start,
			   guint64 addr_end,
			   FwupdInstallFlags flags,
			   GError **error)
{
	FuBcm57xxFirmware *self = FU_BCM57XX_FIRMWARE (firmware);
	gsize bufsz = 0x0;
	guint32 magic = 0;
	guint32 stage1_sz = 0;
	const guint8 *buf = g_bytes_get_data (fw, &bufsz);
	g_autoptr(FuFirmwareImage) img_info2 = NULL;
	g_autoptr(FuFirmwareImage) img_info = NULL;
	g_autoptr(FuFirmwareImage) img_stage1 = NULL;
	g_autoptr(FuFirmwareImage) img_stage2 = NULL;
	g_autoptr(FuFirmwareImage) img_vpd = NULL;
	g_autoptr(GBytes) blob_header = NULL;
	g_autoptr(GBytes) blob_info2 = NULL;
	g_autoptr(GBytes) blob_info = NULL;
	g_autoptr(GBytes) blob_vpd = NULL;

	/* try to autodetect the file type */
	if (!fu_common_read_uint32_safe (buf, bufsz, 0x0, &magic, G_BIG_ENDIAN, error))
		return FALSE;

	/* standalone APE */
	if (magic == BCM_APE_HEADER_MAGIC) {
		g_autoptr(FuFirmwareImage) img = fu_bcm57xx_dict_image_new ();
		fu_bcm57xx_dict_image_set_target (FU_BCM57XX_DICT_IMAGE (img), 0xD);
		fu_bcm57xx_dict_image_set_kind (FU_BCM57XX_DICT_IMAGE (img), 0x0);
		fu_firmware_image_set_bytes (img, fw);
		fu_firmware_image_set_addr (img, BCM_CODE_DIRECTORY_ADDR_APE);
		fu_firmware_image_set_id (img, "ape");
		fu_firmware_add_image (firmware, img);
		return TRUE;
	}

	/* standalone stage1 */
	if (magic == BCM_STAGE1_HEADER_MAGIC_BROADCOM ||
	    magic == BCM_STAGE1_HEADER_MAGIC_MEKLORT) {
		img_stage1 = fu_firmware_image_new (fw);
		fu_firmware_image_set_id (img_stage1, "stage1");
		fu_firmware_add_image (firmware, img_stage1);
		return TRUE;
	}

	/* not full NVRAM image */
	if (magic != BCM_NVRAM_MAGIC) {
		g_set_error (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_NOT_SUPPORTED,
			     "file not supported, got: 0x%08X",
			     magic);
		return FALSE;
	}

	/* save the size so we can export the padding for a perfect roundtrip */
	self->source_size = bufsz;
	self->source_padchar = buf[bufsz - 1];

	/* NVRAM header */
	blob_header = fu_common_bytes_new_offset (fw,
						  BCM_NVRAM_HEADER_BASE,
						  BCM_NVRAM_HEADER_SZ,
						  error);
	if (blob_header == NULL)
		return FALSE;
	if (!fu_bcm57xx_firmware_parse_header (self, blob_header, error)) {
		g_prefix_error (error, "failed to parse header: ");
		return FALSE;
	}

	/* info */
	blob_info = fu_common_bytes_new_offset (fw,
						BCM_NVRAM_INFO_BASE,
						BCM_NVRAM_INFO_SZ,
						error);
	if (blob_info == NULL)
		return FALSE;
	img_info = fu_bcm57xx_firmware_parse_info (self, blob_info, error);
	if (img_info == NULL) {
		g_prefix_error (error, "failed to parse info: ");
		return FALSE;
	}
	fu_firmware_image_set_offset (img_info, BCM_NVRAM_INFO_BASE);
	fu_firmware_add_image (firmware, img_info);

	/* VPD */
	blob_vpd = fu_common_bytes_new_offset (fw,
					       BCM_NVRAM_VPD_BASE,
					       BCM_NVRAM_VPD_SZ,
					       error);
	if (blob_vpd == NULL)
		return FALSE;
	img_vpd = fu_firmware_image_new (blob_vpd);
	fu_firmware_image_set_id (img_vpd, "vpd");
	fu_firmware_image_set_offset (img_vpd, BCM_NVRAM_VPD_BASE);
	fu_firmware_add_image (firmware, img_vpd);

	/* info2 */
	blob_info2 = fu_common_bytes_new_offset (fw,
						 BCM_NVRAM_INFO2_BASE,
						 BCM_NVRAM_INFO2_SZ,
						 error);
	if (blob_info2 == NULL)
		return FALSE;
	img_info2 = fu_firmware_image_new (blob_info2);
	fu_firmware_image_set_id (img_info2, "info2");
	fu_firmware_image_set_offset (img_info2, BCM_NVRAM_INFO2_BASE);
	fu_firmware_add_image (firmware, img_info2);

	/* stage1 */
	img_stage1 = fu_bcm57xx_firmware_parse_stage1 (self, fw, &stage1_sz, flags, error);
	if (img_stage1 == NULL) {
		g_prefix_error (error, "failed to parse stage1: ");
		return FALSE;
	}
	fu_firmware_add_image (firmware, img_stage1);

	/* stage2 */
	img_stage2 = fu_bcm57xx_firmware_parse_stage2 (self, fw, stage1_sz, flags, error);
	if (img_stage2 == NULL) {
		g_prefix_error (error, "failed to parse stage2: ");
		return FALSE;
	}
	fu_firmware_add_image (firmware, img_stage2);

	/* dictionaries, e.g. APE */
	for (guint i = 0; i < 8; i++) {
		if (!fu_bcm57xx_firmware_parse_dict (self, fw, i, flags, error)) {
			g_prefix_error (error, "failed to parse dict 0x%x: ", i);
			return FALSE;
		}
	}

	/* success */
	return TRUE;
}

static void
_g_byte_array_append_bytes (GByteArray *buf, GBytes *bytes)
{
	g_byte_array_append (buf, g_bytes_get_data (bytes, NULL), g_bytes_get_size (bytes));
}

static GBytes *
_g_bytes_new_sized (gsize sz)
{
	GByteArray *tmp = g_byte_array_sized_new (sz);
	for (gsize i = 0; i < sz; i++)
		fu_byte_array_append_uint8 (tmp, 0x0);
	return g_byte_array_free_to_bytes (tmp);
}

static gboolean
fu_bcm57xx_firmware_build (FuFirmware *firmware, XbNode *n, GError **error)
{
	FuBcm57xxFirmware *self = FU_BCM57XX_FIRMWARE (firmware);
	guint64 tmp;

	/* two simple properties */
	tmp = xb_node_query_text_as_uint (n, "vendor", NULL);
	if (tmp != G_MAXUINT64 && tmp <= G_MAXUINT16)
		self->vendor = tmp;
	tmp = xb_node_query_text_as_uint (n, "model", NULL);
	if (tmp != G_MAXUINT64 && tmp <= G_MAXUINT16)
		self->model = tmp;

	/* success */
	return TRUE;
}

static GBytes *
fu_bcm57xx_firmware_write (FuFirmware *firmware, GError **error)
{
	gsize off = BCM_NVRAM_STAGE1_BASE;
	FuBcm57xxFirmware *self = FU_BCM57XX_FIRMWARE (firmware);
	g_autoptr(GByteArray) buf = g_byte_array_sized_new (self->source_size);
	g_autoptr(FuFirmwareImage) img_info2 = NULL;
	g_autoptr(FuFirmwareImage) img_info = NULL;
	g_autoptr(FuFirmwareImage) img_stage1 = NULL;
	g_autoptr(FuFirmwareImage) img_stage2 = NULL;
	g_autoptr(FuFirmwareImage) img_vpd = NULL;
	g_autoptr(GBytes) blob_info2 = NULL;
	g_autoptr(GBytes) blob_info = NULL;
	g_autoptr(GBytes) blob_stage1 = NULL;
	g_autoptr(GBytes) blob_stage2 = NULL;
	g_autoptr(GBytes) blob_vpd = NULL;
	g_autoptr(GPtrArray) blob_dicts = NULL;

	/* write out the things we need to pre-compute */
	img_stage1 = fu_firmware_get_image_by_id (firmware, "stage1", error);
	if (img_stage1 == NULL)
		return NULL;
	blob_stage1 = fu_firmware_image_write (img_stage1, error);
	if (blob_stage1 == NULL)
		return NULL;
	off += g_bytes_get_size (blob_stage1);
	img_stage2 = fu_firmware_get_image_by_id (firmware, "stage2", error);
	if (img_stage2 == NULL)
		return NULL;
	blob_stage2 = fu_firmware_image_write (img_stage2, error);
	if (blob_stage2 == NULL)
		return NULL;
	off += g_bytes_get_size (blob_stage2);

	/* add header */
	fu_byte_array_append_uint32 (buf, BCM_NVRAM_MAGIC, G_BIG_ENDIAN);
	fu_byte_array_append_uint32 (buf, self->phys_addr, G_BIG_ENDIAN);
	fu_byte_array_append_uint32 (buf, g_bytes_get_size (blob_stage1) / sizeof(guint32), G_BIG_ENDIAN);
	fu_byte_array_append_uint32 (buf, BCM_NVRAM_STAGE1_BASE, G_BIG_ENDIAN);
	fu_byte_array_append_uint32 (buf, fu_bcm57xx_nvram_crc (buf->data, buf->len), G_LITTLE_ENDIAN);

	/* add directory entries */
	blob_dicts = g_ptr_array_new_with_free_func ((GDestroyNotify) g_bytes_unref);
	for (guint i = 0; i < 8; i++) {
		g_autoptr(FuFirmwareImage) img = NULL;
		g_autoptr(GBytes) blob = NULL;

		img = fu_firmware_get_image_by_idx (firmware, 0x80 + i, NULL);
		if (img != NULL) {
			blob = fu_firmware_image_write (img, error);
			if (blob == NULL)
				return NULL;
		}
		if (blob != NULL) {
			fu_byte_array_append_uint32 (buf, fu_firmware_image_get_addr (img), G_BIG_ENDIAN);
			fu_byte_array_append_uint32 (buf,
						     (g_bytes_get_size (blob) / sizeof(guint32)) |
						     (guint32) fu_bcm57xx_dict_image_get_target (FU_BCM57XX_DICT_IMAGE (img)) << 24 |
						     (guint32) fu_bcm57xx_dict_image_get_kind (FU_BCM57XX_DICT_IMAGE (img)) << 28,
						     G_BIG_ENDIAN);
			if (g_bytes_get_size (blob) > 0) {
				fu_byte_array_append_uint32 (buf, off, G_BIG_ENDIAN);
				off += g_bytes_get_size (blob);
			} else {
				fu_byte_array_append_uint32 (buf, 0x0, G_BIG_ENDIAN);
			}
		} else {
			blob = g_bytes_new (NULL, 0);
			for (guint32 j = 0; j < sizeof(guint32) * 3; j++)
				fu_byte_array_append_uint8 (buf, 0x0);
		}
		g_ptr_array_add (blob_dicts, g_steal_pointer (&blob));
	}

	/* add info */
	img_info = fu_firmware_get_image_by_id (firmware, "info", NULL);
	if (img_info != NULL) {
		blob_info = fu_firmware_image_write (img_info, error);
		if (blob_info == NULL)
			return NULL;
	} else {
		GByteArray *tmp = g_byte_array_sized_new (BCM_NVRAM_INFO_SZ);
		for (gsize i = 0; i < BCM_NVRAM_INFO_SZ; i++)
			fu_byte_array_append_uint8 (tmp, 0x0);
		fu_common_write_uint16 (tmp->data + BCM_NVRAM_INFO_VENDOR,
					self->vendor, G_BIG_ENDIAN);
		fu_common_write_uint16 (tmp->data + BCM_NVRAM_INFO_DEVICE,
					self->model, G_BIG_ENDIAN);
		blob_info = g_byte_array_free_to_bytes (tmp);
	}
	_g_byte_array_append_bytes (buf, blob_info);

	/* add vpd */
	img_vpd = fu_firmware_get_image_by_id (firmware, "vpd", NULL);
	if (img_vpd != NULL) {
		blob_vpd = fu_firmware_image_write (img_vpd, error);
		if (blob_vpd == NULL)
			return NULL;
	} else {
		blob_vpd = _g_bytes_new_sized (BCM_NVRAM_VPD_SZ);
	}
	_g_byte_array_append_bytes (buf, blob_vpd);

	/* add info2 */
	img_info2 = fu_firmware_get_image_by_id (firmware, "info2", NULL);
	if (img_info2 != NULL) {
		blob_info2 = fu_firmware_image_write (img_info2, error);
		if (blob_info2 == NULL)
			return NULL;
	} else {
		blob_info2 = _g_bytes_new_sized (BCM_NVRAM_INFO2_SZ);
	}
	_g_byte_array_append_bytes (buf, blob_info2);

	/* add stage1+2 */
	_g_byte_array_append_bytes (buf, blob_stage1);
	_g_byte_array_append_bytes (buf, blob_stage2);

	/* add dictionaries, e.g. APE */
	for (guint i = 0; i < blob_dicts->len; i++) {
		GBytes *blob = g_ptr_array_index (blob_dicts, i);
		_g_byte_array_append_bytes (buf, blob);
	}

	/* pad until full */
	for (guint32 i = buf->len; i < self->source_size; i++)
		fu_byte_array_append_uint8 (buf, self->source_padchar);

	/* add EOF */
	return g_byte_array_free_to_bytes (g_steal_pointer (&buf));
}

guint16
fu_bcm57xx_firmware_get_vendor (FuBcm57xxFirmware *self)
{
	return self->vendor;
}

guint16
fu_bcm57xx_firmware_get_model (FuBcm57xxFirmware *self)
{
	return self->model;
}

gboolean
fu_bcm57xx_firmware_is_backup (FuBcm57xxFirmware *self)
{
	return self->is_backup;
}

static void
fu_bcm57xx_firmware_init (FuBcm57xxFirmware *self)
{
	self->phys_addr = BCM_PHYS_ADDR_DEFAULT;
	self->source_size = BCM_FIRMWARE_SIZE;
	self->source_padchar = 0xff;
	fu_firmware_add_flag (FU_FIRMWARE (self), FU_FIRMWARE_FLAG_DEDUPE_ID);
	fu_firmware_add_flag (FU_FIRMWARE (self), FU_FIRMWARE_FLAG_HAS_CHECKSUM);
	fu_firmware_add_flag (FU_FIRMWARE (self), FU_FIRMWARE_FLAG_HAS_VID_PID);
}

static void
fu_bcm57xx_firmware_class_init (FuBcm57xxFirmwareClass *klass)
{
	FuFirmwareClass *klass_firmware = FU_FIRMWARE_CLASS (klass);
	klass_firmware->parse = fu_bcm57xx_firmware_parse;
	klass_firmware->to_string = fu_bcm57xx_firmware_to_string;
	klass_firmware->write = fu_bcm57xx_firmware_write;
	klass_firmware->build = fu_bcm57xx_firmware_build;
}

FuFirmware *
fu_bcm57xx_firmware_new (void)
{
	return FU_FIRMWARE (g_object_new (FU_TYPE_BCM57XX_FIRMWARE, NULL));
}
