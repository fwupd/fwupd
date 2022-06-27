/*
 * Copyright (C) 2017 Intel Corporation.
 * Copyright (C) 2020 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include <fwupdplugin.h>

#include "fu-thunderbolt-firmware.h"

typedef struct {
	guint32 sections[_SECTION_LAST];
	FuThunderboltFamily family;
	gboolean is_host;
	gboolean is_native;
	gboolean has_pd;
	guint16 device_id;
	guint16 vendor_id;
	guint16 model_id;
	guint gen;
	guint ports;
	guint8 flash_size;
} FuThunderboltFirmwarePrivate;

G_DEFINE_TYPE_WITH_PRIVATE(FuThunderboltFirmware, fu_thunderbolt_firmware, FU_TYPE_FIRMWARE)

#define GET_PRIVATE(o) (fu_thunderbolt_firmware_get_instance_private(o))

typedef struct {
	guint16 id;
	guint gen;
	FuThunderboltFamily family;
	guint ports;
} FuThunderboltHwInfo;

enum {
	DROM_ENTRY_MC = 0x6,
};

gboolean
fu_thunderbolt_firmware_is_host(FuThunderboltFirmware *self)
{
	FuThunderboltFirmwarePrivate *priv;
	g_return_val_if_fail(FU_IS_THUNDERBOLT_FIRMWARE(self), FALSE);
	priv = GET_PRIVATE(self);
	return priv->is_host;
}

gboolean
fu_thunderbolt_firmware_is_native(FuThunderboltFirmware *self)
{
	FuThunderboltFirmwarePrivate *priv;
	g_return_val_if_fail(FU_IS_THUNDERBOLT_FIRMWARE(self), FALSE);
	priv = GET_PRIVATE(self);
	return priv->is_native;
}

gboolean
fu_thunderbolt_firmware_get_has_pd(FuThunderboltFirmware *self)
{
	FuThunderboltFirmwarePrivate *priv;
	g_return_val_if_fail(FU_IS_THUNDERBOLT_FIRMWARE(self), FALSE);
	priv = GET_PRIVATE(self);
	return priv->has_pd;
}

guint16
fu_thunderbolt_firmware_get_device_id(FuThunderboltFirmware *self)
{
	FuThunderboltFirmwarePrivate *priv;
	g_return_val_if_fail(FU_IS_THUNDERBOLT_FIRMWARE(self), 0x0);
	priv = GET_PRIVATE(self);
	return priv->device_id;
}

guint16
fu_thunderbolt_firmware_get_vendor_id(FuThunderboltFirmware *self)
{
	FuThunderboltFirmwarePrivate *priv;
	g_return_val_if_fail(FU_IS_THUNDERBOLT_FIRMWARE(self), 0x0);
	priv = GET_PRIVATE(self);
	return priv->vendor_id;
}

guint16
fu_thunderbolt_firmware_get_model_id(FuThunderboltFirmware *self)
{
	FuThunderboltFirmwarePrivate *priv;
	g_return_val_if_fail(FU_IS_THUNDERBOLT_FIRMWARE(self), 0x0);
	priv = GET_PRIVATE(self);
	return priv->model_id;
}

guint8
fu_thunderbolt_firmware_get_flash_size(FuThunderboltFirmware *self)
{
	FuThunderboltFirmwarePrivate *priv;
	g_return_val_if_fail(FU_IS_THUNDERBOLT_FIRMWARE(self), 0x0);
	priv = GET_PRIVATE(self);
	return priv->flash_size;
}

static const gchar *
fu_thunderbolt_firmware_family_to_string(FuThunderboltFamily family)
{
	if (family == _FAMILY_FR)
		return "Falcon Ridge";
	if (family == _FAMILY_WR)
		return "Win Ridge";
	if (family == _FAMILY_AR)
		return "Alpine Ridge";
	if (family == _FAMILY_AR_C)
		return "Alpine Ridge C";
	if (family == _FAMILY_TR)
		return "Titan Ridge";
	if (family == _FAMILY_BB)
		return "BB";
	if (family == _FAMILY_MR)
		return "Maple Ridge";
	return "Unknown";
}

static void
fu_thunderbolt_firmware_export(FuFirmware *firmware, FuFirmwareExportFlags flags, XbBuilderNode *bn)
{
	FuThunderboltFirmware *self = FU_THUNDERBOLT_FIRMWARE(firmware);
	FuThunderboltFirmwarePrivate *priv = GET_PRIVATE(self);

	fu_xmlb_builder_insert_kv(bn,
				  "family",
				  fu_thunderbolt_firmware_family_to_string(priv->family));
	fu_xmlb_builder_insert_kb(bn, "is_host", priv->is_host);
	fu_xmlb_builder_insert_kb(bn, "is_native", priv->is_native);
	fu_xmlb_builder_insert_kx(bn, "device_id", priv->device_id);
	fu_xmlb_builder_insert_kx(bn, "vendor_id", priv->vendor_id);
	fu_xmlb_builder_insert_kx(bn, "model_id", priv->model_id);
	fu_xmlb_builder_insert_kx(bn, "flash_size", priv->flash_size);
	fu_xmlb_builder_insert_kx(bn, "generation", priv->gen);
	fu_xmlb_builder_insert_kx(bn, "ports", priv->ports);
	fu_xmlb_builder_insert_kb(bn, "has_pd", priv->has_pd);
	for (guint i = 0; i < _SECTION_LAST; i++) {
		g_autofree gchar *tmp = g_strdup_printf("%x", priv->sections[i]);
		xb_builder_node_insert_text(bn, "section", tmp, NULL);
	}
}

static inline gboolean
fu_thunderbolt_firmware_valid_pd_pointer(guint32 pointer)
{
	return pointer != 0 && pointer != 0xFFFFFFFF;
}

gboolean
fu_thunderbolt_firmware_read_location(FuThunderboltFirmware *self,
				      FuThunderboltSection section,
				      guint32 offset,
				      guint8 *buf,
				      guint32 len,
				      GError **error)
{
	const guint8 *srcbuf;
	gsize srcbufsz = 0;
	FuThunderboltFirmwarePrivate *priv = GET_PRIVATE(self);
	guint32 location_start = priv->sections[section] + offset;
	g_autoptr(GBytes) fw = NULL;

	/* get blob */
	fw = fu_firmware_get_bytes(FU_FIRMWARE(self), error);
	if (fw == NULL)
		return FALSE;
	srcbuf = g_bytes_get_data(fw, &srcbufsz);

	if (!fu_memcpy_safe(buf,
			    len,
			    0x0, /* dst */
			    srcbuf,
			    srcbufsz,
			    location_start, /* src */
			    len,
			    error)) {
		g_prefix_error(error, "location is outside of the given image: ");
		return FALSE;
	}
	return TRUE;
}

static gboolean
fu_thunderbolt_firmware_read_uint8(FuThunderboltFirmware *self,
				   FuThunderboltSection section,
				   guint32 offset,
				   guint8 *value,
				   GError **error)
{
	return fu_thunderbolt_firmware_read_location(self, section, offset, value, 1, error);
}

static gboolean
fu_thunderbolt_firmware_read_uint16(FuThunderboltFirmware *self,
				    FuThunderboltSection section,
				    guint32 offset,
				    guint16 *value,
				    GError **error)
{
	guint16 tmp = 0;
	if (!fu_thunderbolt_firmware_read_location(self,
						   section,
						   offset,
						   (guint8 *)&tmp,
						   sizeof(tmp),
						   error)) {
		g_prefix_error(error, "failed to read uint16: ");
		return FALSE;
	}
	*value = GUINT16_FROM_LE(tmp);
	return TRUE;
}

static gboolean
fu_thunderbolt_firmware_read_uint32(FuThunderboltFirmware *self,
				    FuThunderboltSection section,
				    guint32 offset,
				    guint32 *value,
				    GError **error)
{
	guint32 tmp = 0;
	if (!fu_thunderbolt_firmware_read_location(self,
						   section,
						   offset,
						   (guint8 *)&tmp,
						   sizeof(tmp),
						   error)) {
		g_prefix_error(error, "failed to read uint32: ");
		return FALSE;
	}
	*value = GUINT32_FROM_LE(tmp);
	return TRUE;
}

/*
 * Size of ucode sections is uint16 value saved at the start of the section,
 * it's in DWORDS (4-bytes) units and it doesn't include itself. We need the
 * offset to the next section, so we translate it to bytes and add 2 for the
 * size field itself.
 *
 * offset parameter must be relative to digital section
 */
static gboolean
fu_thunderbolt_firmware_read_ucode_section_len(FuThunderboltFirmware *self,
					       guint32 offset,
					       guint16 *value,
					       GError **error)
{
	if (!fu_thunderbolt_firmware_read_uint16(self, _SECTION_DIGITAL, offset, value, error)) {
		g_prefix_error(error, "failed to read ucode section len: ");
		return FALSE;
	}
	*value *= sizeof(guint32);
	*value += sizeof(guint16);
	return TRUE;
}

/* assumes sections[_SECTION_DIGITAL].offset is already set */
static gboolean
fu_thunderbolt_firmware_read_sections(FuThunderboltFirmware *self, GError **error)
{
	guint32 offset;
	FuThunderboltFirmwarePrivate *priv = GET_PRIVATE(self);

	if (priv->gen >= 3 || priv->gen == 0) {
		if (!fu_thunderbolt_firmware_read_uint32(self,
							 _SECTION_DIGITAL,
							 0x10e,
							 &offset,
							 error))
			return FALSE;
		priv->sections[_SECTION_DROM] = offset + priv->sections[_SECTION_DIGITAL];

		if (!fu_thunderbolt_firmware_read_uint32(self,
							 _SECTION_DIGITAL,
							 0x75,
							 &offset,
							 error))
			return FALSE;
		priv->sections[_SECTION_ARC_PARAMS] = offset + priv->sections[_SECTION_DIGITAL];
	}

	if (priv->is_host && priv->gen > 2) {
		/*
		 * To find the DRAM section, we have to jump from section to
		 * section in a chain of sections.
		 * available_sections location tells what sections exist at all
		 * (with a flag per section).
		 * ee_ucode_start_addr location tells the offset of the first
		 * section in the list relatively to the digital section start.
		 * After having the offset of the first section, we have a loop
		 * over the section list. If the section exists, we read its
		 * length (2 bytes at section start) and add it to current
		 * offset to find the start of the next section. Otherwise, we
		 * already have the next section offset...
		 */
		const guint8 DRAM_FLAG = 1 << 6;
		guint16 ucode_offset;
		guint8 available_sections = 0;

		if (!fu_thunderbolt_firmware_read_uint8(self,
							_SECTION_DIGITAL,
							0x2,
							&available_sections,
							error)) {
			g_prefix_error(error, "failed to read available sections: ");
			return FALSE;
		}
		if (!fu_thunderbolt_firmware_read_uint16(self,
							 _SECTION_DIGITAL,
							 0x3,
							 &ucode_offset,
							 error)) {
			g_prefix_error(error, "failed to read ucode offset: ");
			return FALSE;
		}
		offset = ucode_offset;
		if ((available_sections & DRAM_FLAG) == 0) {
			g_set_error_literal(error,
					    FWUPD_ERROR,
					    FWUPD_ERROR_INVALID_FILE,
					    "Can't find needed FW sections in the FW image file");
			return FALSE;
		}

		for (guint8 i = 1; i < DRAM_FLAG; i <<= 1) {
			if (available_sections & i) {
				if (!fu_thunderbolt_firmware_read_ucode_section_len(self,
										    offset,
										    &ucode_offset,
										    error))
					return FALSE;
				offset += ucode_offset;
			}
		}
		priv->sections[_SECTION_DRAM_UCODE] = offset + priv->sections[_SECTION_DIGITAL];
	}

	return TRUE;
}

static gboolean
fu_thunderbolt_firmware_missing_needed_drom(FuThunderboltFirmware *self)
{
	FuThunderboltFirmwarePrivate *priv = GET_PRIVATE(self);
	if (priv->sections[_SECTION_DROM] != 0)
		return FALSE;
	if (priv->is_host && priv->gen < 3)
		return FALSE;
	return TRUE;
}

void
fu_thunderbolt_firmware_set_digital(FuThunderboltFirmware *self, guint32 offset)
{
	FuThunderboltFirmwarePrivate *priv = GET_PRIVATE(self);
	priv->sections[_SECTION_DIGITAL] = offset;
}

static gboolean
fu_thunderbolt_firmware_parse(FuFirmware *firmware,
			      GBytes *fw,
			      gsize offset,
			      FwupdInstallFlags flags,
			      GError **error)
{
	FuThunderboltFirmware *self = FU_THUNDERBOLT_FIRMWARE(firmware);
	FuThunderboltFirmwarePrivate *priv = GET_PRIVATE(self);
	FuThunderboltFirmwareClass *klass_firmware = FU_THUNDERBOLT_FIRMWARE_GET_CLASS(firmware);

	guint8 tmp = 0;
	guint16 version = 0;
	static const FuThunderboltHwInfo hw_info_arr[] = {
	    {0x156D, 2, _FAMILY_FR, 2},	  /* FR 4C */
	    {0x156B, 2, _FAMILY_FR, 1},	  /* FR 2C */
	    {0x157E, 2, _FAMILY_WR, 1},	  /* WR */
	    {0x1578, 3, _FAMILY_AR, 2},	  /* AR 4C */
	    {0x1576, 3, _FAMILY_AR, 1},	  /* AR 2C */
	    {0x15C0, 3, _FAMILY_AR, 1},	  /* AR LP */
	    {0x15D3, 3, _FAMILY_AR_C, 2}, /* AR-C 4C */
	    {0x15DA, 3, _FAMILY_AR_C, 1}, /* AR-C 2C */
	    {0x15E7, 3, _FAMILY_TR, 1},	  /* TR 2C */
	    {0x15EA, 3, _FAMILY_TR, 2},	  /* TR 4C */
	    {0x15EF, 3, _FAMILY_TR, 2},	  /* TR 4C device */
	    {0x15EE, 3, _FAMILY_BB, 0},	  /* BB device */
	    /* Maple ridge devices
	     * NOTE: These are expected to be flashed via UEFI capsules *not* Thunderbolt plugin
	     * Flashing via fwupd will require matching kernel work.
	     * They're left here only for parsing the binaries
	     */
	    {0x1136, 4, _FAMILY_MR, 2},
	    {0x1137, 4, _FAMILY_MR, 2},
	    {0}};
	g_autofree gchar *version_str = NULL;

	/* add this straight away so we can read it without a self */
	fu_firmware_set_bytes(firmware, fw);

	/* subclassed */
	if (klass_firmware->parse != NULL) {
		if (!klass_firmware->parse(firmware, fw, offset, flags, error))
			return FALSE;
	}

	/* is native */
	if (!fu_thunderbolt_firmware_read_uint8(self,
						_SECTION_DIGITAL,
						FU_TBT_OFFSET_NATIVE,
						&tmp,
						error)) {
		g_prefix_error(error, "failed to read native: ");
		return FALSE;
	}
	priv->is_native = tmp & 0x20;

	/* we're only reading the first chunk */
	if (g_bytes_get_size(fw) == 0x80)
		return TRUE;

	/* host or device */
	if (!fu_thunderbolt_firmware_read_uint8(self, _SECTION_DIGITAL, 0x10, &tmp, error)) {
		g_prefix_error(error, "failed to read is-host: ");
		return FALSE;
	}
	priv->is_host = tmp & (1 << 1);

	/* device ID */
	if (!fu_thunderbolt_firmware_read_uint16(self,
						 _SECTION_DIGITAL,
						 0x5,
						 &priv->device_id,
						 error)) {
		g_prefix_error(error, "failed to read device-id: ");
		return FALSE;
	}

	/* this is best-effort */
	for (guint i = 0; hw_info_arr[i].id != 0; i++) {
		if (hw_info_arr[i].id == priv->device_id) {
			priv->family = hw_info_arr[i].family;
			priv->gen = hw_info_arr[i].gen;
			priv->ports = hw_info_arr[i].ports;
			break;
		}
	}
	if (priv->ports == 0 && priv->is_host) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_NOT_SUPPORTED,
			    "Unknown controller: %x",
			    priv->device_id);
		return FALSE;
	}

	/* read sections from file */
	if (!fu_thunderbolt_firmware_read_sections(self, error))
		return FALSE;
	if (fu_thunderbolt_firmware_missing_needed_drom(self)) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_READ,
				    "Can't find required FW sections");
		return FALSE;
	}

	/* vendor:model */
	if (priv->sections[_SECTION_DROM] != 0) {
		if (!fu_thunderbolt_firmware_read_uint16(self,
							 _SECTION_DROM,
							 0x10,
							 &priv->vendor_id,
							 error)) {
			g_prefix_error(error, "failed to read vendor-id: ");
			return FALSE;
		}
		if (!fu_thunderbolt_firmware_read_uint16(self,
							 _SECTION_DROM,
							 0x12,
							 &priv->model_id,
							 error)) {
			g_prefix_error(error, "failed to read model-id: ");
			return FALSE;
		}
	}

	/* has PD */
	if (priv->sections[_SECTION_ARC_PARAMS] != 0) {
		guint32 pd_pointer = 0x0;
		if (!fu_thunderbolt_firmware_read_uint32(self,
							 _SECTION_ARC_PARAMS,
							 0x10C,
							 &pd_pointer,
							 error)) {
			g_prefix_error(error, "failed to read pd-pointer: ");
			return FALSE;
		}
		priv->has_pd = fu_thunderbolt_firmware_valid_pd_pointer(pd_pointer);
	}

	/* versions */
	switch (priv->family) {
	case _FAMILY_TR:
		if (!fu_thunderbolt_firmware_read_uint16(self,
							 _SECTION_DIGITAL,
							 0x09,
							 &version,
							 error)) {
			g_prefix_error(error, "failed to read version: ");
			return FALSE;
		}
		version_str = fu_version_from_uint16(version, FWUPD_VERSION_FORMAT_BCD);
		fu_firmware_set_version(FU_FIRMWARE(self), version_str);
		break;
	default:
		break;
	}

	if (priv->is_host) {
		switch (priv->family) {
		case _FAMILY_AR:
		case _FAMILY_AR_C:
		case _FAMILY_TR:
			/* This is used for comparison between old and new image, not a raw number
			 */
			if (!fu_thunderbolt_firmware_read_uint8(self,
								_SECTION_DIGITAL,
								0x45,
								&tmp,
								error)) {
				g_prefix_error(error, "failed to read flash size: ");
				return FALSE;
			}
			priv->flash_size = tmp & 0x07;
			break;
		default:
			break;
		}
	}

	/* success */
	return TRUE;
}

static void
fu_thunderbolt_firmware_init(FuThunderboltFirmware *self)
{
	fu_firmware_add_flag(FU_FIRMWARE(self), FU_FIRMWARE_FLAG_HAS_VID_PID);
}

static void
fu_thunderbolt_firmware_class_init(FuThunderboltFirmwareClass *klass)
{
	FuFirmwareClass *klass_firmware = FU_FIRMWARE_CLASS(klass);
	klass_firmware->parse = fu_thunderbolt_firmware_parse;
	klass_firmware->export = fu_thunderbolt_firmware_export;
}

FuThunderboltFirmware *
fu_thunderbolt_firmware_new(void)
{
	return g_object_new(FU_TYPE_THUNDERBOLT_FIRMWARE, NULL);
}
