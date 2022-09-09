/*
 * Copyright (C) 2021 Dell Inc.
 * Copyright (C) 2020 Richard Hughes <richard@hughsie.com>
 * Copyright (C) 2020 Mario Limonciello <mario.limonciello@dell.com>
 * Copyright (C) 2017 Intel Corporation.
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#define G_LOG_DOMAIN "FuFirmware"

#include "config.h"

#include "fu-byte-array.h"
#include "fu-bytes.h"
#include "fu-intel-thunderbolt-nvm.h"
#include "fu-mem.h"
#include "fu-string.h"
#include "fu-version-common.h"

/**
 * FuIntelThunderboltNvm:
 *
 * The Non-Volatile-Memory device specification. This is what you would find on the device SPI chip.
 *
 * See also: [class@FuFirmware]
 */

typedef enum {
	FU_INTEL_THUNDERBOLT_NVM_SECTION_DIGITAL,
	FU_INTEL_THUNDERBOLT_NVM_SECTION_DROM,
	FU_INTEL_THUNDERBOLT_NVM_SECTION_ARC_PARAMS,
	FU_INTEL_THUNDERBOLT_NVM_SECTION_DRAM_UCODE,
	FU_INTEL_THUNDERBOLT_NVM_SECTION_LAST
} FuIntelThunderboltNvmSection;

typedef enum {
	FU_INTEL_THUNDERBOLT_NVM_FAMILY_UNKNOWN,
	FU_INTEL_THUNDERBOLT_NVM_FAMILY_FR,
	FU_INTEL_THUNDERBOLT_NVM_FAMILY_WR,
	FU_INTEL_THUNDERBOLT_NVM_FAMILY_AR,
	FU_INTEL_THUNDERBOLT_NVM_FAMILY_AR_C,
	FU_INTEL_THUNDERBOLT_NVM_FAMILY_TR,
	FU_INTEL_THUNDERBOLT_NVM_FAMILY_BB,
	FU_INTEL_THUNDERBOLT_NVM_FAMILY_MR,
	FU_INTEL_THUNDERBOLT_NVM_FAMILY_GR,
	FU_INTEL_THUNDERBOLT_NVM_FAMILY_LAST,
} FuIntelThunderboltNvmFamily;

typedef struct {
	guint32 sections[FU_INTEL_THUNDERBOLT_NVM_SECTION_LAST];
	FuIntelThunderboltNvmFamily family;
	gboolean is_host;
	gboolean is_native;
	gboolean has_pd;
	guint16 vendor_id;
	guint16 device_id;
	guint16 model_id;
	guint gen;
	guint ports;
	guint8 flash_size;
} FuIntelThunderboltNvmPrivate;

G_DEFINE_TYPE_WITH_PRIVATE(FuIntelThunderboltNvm, fu_intel_thunderbolt_nvm, FU_TYPE_FIRMWARE)
#define GET_PRIVATE(o) (fu_intel_thunderbolt_nvm_get_instance_private(o))

#define FU_INTEL_THUNDERBOLT_NVM_DIGITAL_OFFSET_AVAILABLE_SECTIONS 0x0002
#define FU_INTEL_THUNDERBOLT_NVM_DIGITAL_OFFSET_UCODE		   0x0003
#define FU_INTEL_THUNDERBOLT_NVM_DIGITAL_OFFSET_DEVICE_ID	   0x0005
#define FU_INTEL_THUNDERBOLT_NVM_DIGITAL_OFFSET_VERSION		   0x0009
#define FU_INTEL_THUNDERBOLT_NVM_DIGITAL_OFFSET_FLAGS_HOST	   0x0010
#define FU_INTEL_THUNDERBOLT_NVM_DIGITAL_OFFSET_FLASH_SIZE	   0x0045
#define FU_INTEL_THUNDERBOLT_NVM_DIGITAL_OFFSET_ARC_PARAMS	   0x0075
#define FU_INTEL_THUNDERBOLT_NVM_DIGITAL_OFFSET_FLAGS_IS_NATIVE	   0x007B
#define FU_INTEL_THUNDERBOLT_NVM_DIGITAL_OFFSET_DROM		   0x010E

#define FU_INTEL_THUNDERBOLT_NVM_DROM_OFFSET_VENDOR_ID 0x0010
#define FU_INTEL_THUNDERBOLT_NVM_DROM_OFFSET_MODEL_ID  0x0012

#define FU_INTEL_THUNDERBOLT_NVM_ARC_PARAMS_OFFSET_PD_POINTER 0x010C

static const gchar *
fu_intel_thunderbolt_nvm_family_to_string(FuIntelThunderboltNvmFamily family)
{
	if (family == FU_INTEL_THUNDERBOLT_NVM_FAMILY_FR)
		return "falcon-ridge";
	if (family == FU_INTEL_THUNDERBOLT_NVM_FAMILY_WR)
		return "win-ridge";
	if (family == FU_INTEL_THUNDERBOLT_NVM_FAMILY_AR)
		return "alpine-ridge";
	if (family == FU_INTEL_THUNDERBOLT_NVM_FAMILY_AR_C)
		return "alpine-ridge-c";
	if (family == FU_INTEL_THUNDERBOLT_NVM_FAMILY_TR)
		return "titan-ridge";
	if (family == FU_INTEL_THUNDERBOLT_NVM_FAMILY_BB)
		return "bb";
	if (family == FU_INTEL_THUNDERBOLT_NVM_FAMILY_MR)
		return "maple-ridge";
	if (family == FU_INTEL_THUNDERBOLT_NVM_FAMILY_GR)
		return "goshen-ridge";
	return "unknown";
}

static FuIntelThunderboltNvmFamily
fu_intel_thunderbolt_nvm_family_from_string(const gchar *family)
{
	if (g_strcmp0(family, "falcon-ridge") == 0)
		return FU_INTEL_THUNDERBOLT_NVM_FAMILY_FR;
	if (g_strcmp0(family, "win-ridge") == 0)
		return FU_INTEL_THUNDERBOLT_NVM_FAMILY_WR;
	if (g_strcmp0(family, "alpine-ridge") == 0)
		return FU_INTEL_THUNDERBOLT_NVM_FAMILY_AR;
	if (g_strcmp0(family, "alpine-ridge-c") == 0)
		return FU_INTEL_THUNDERBOLT_NVM_FAMILY_AR_C;
	if (g_strcmp0(family, "titan-ridge") == 0)
		return FU_INTEL_THUNDERBOLT_NVM_FAMILY_TR;
	if (g_strcmp0(family, "bb") == 0)
		return FU_INTEL_THUNDERBOLT_NVM_FAMILY_BB;
	if (g_strcmp0(family, "maple-ridge") == 0)
		return FU_INTEL_THUNDERBOLT_NVM_FAMILY_MR;
	if (g_strcmp0(family, "goshen-ridge") == 0)
		return FU_INTEL_THUNDERBOLT_NVM_FAMILY_GR;
	return FU_INTEL_THUNDERBOLT_NVM_FAMILY_UNKNOWN;
}

static const gchar *
fu_intel_thunderbolt_nvm_section_to_string(FuIntelThunderboltNvmSection section)
{
	if (section == FU_INTEL_THUNDERBOLT_NVM_SECTION_DIGITAL)
		return "digital";
	if (section == FU_INTEL_THUNDERBOLT_NVM_SECTION_DROM)
		return "drom";
	if (section == FU_INTEL_THUNDERBOLT_NVM_SECTION_ARC_PARAMS)
		return "arc-params";
	if (section == FU_INTEL_THUNDERBOLT_NVM_SECTION_DRAM_UCODE)
		return "dram-ucode";
	return "unknown";
}

/**
 * fu_intel_thunderbolt_nvm_get_vendor_id:
 * @self: a #FuFirmware
 *
 * Gets the vendor ID.
 *
 * Returns: an integer, or 0x0 for unset
 *
 * Since: 1.8.5
 **/
guint16
fu_intel_thunderbolt_nvm_get_vendor_id(FuIntelThunderboltNvm *self)
{
	FuIntelThunderboltNvmPrivate *priv = GET_PRIVATE(self);
	g_return_val_if_fail(FU_IS_INTEL_THUNDERBOLT_NVM(self), G_MAXUINT16);
	return priv->vendor_id;
}

/**
 * fu_intel_thunderbolt_nvm_get_device_id:
 * @self: a #FuFirmware
 *
 * Gets the device ID.
 *
 * Returns: an integer, or 0x0 for unset
 *
 * Since: 1.8.5
 **/
guint16
fu_intel_thunderbolt_nvm_get_device_id(FuIntelThunderboltNvm *self)
{
	FuIntelThunderboltNvmPrivate *priv = GET_PRIVATE(self);
	return priv->device_id;
}

/**
 * fu_intel_thunderbolt_nvm_is_host:
 * @self: a #FuFirmware
 *
 * Gets if the firmware is designed for a host controller rather than a device.
 *
 * Returns: %TRUE for controller, %FALSE for device
 *
 * Since: 1.8.5
 **/
gboolean
fu_intel_thunderbolt_nvm_is_host(FuIntelThunderboltNvm *self)
{
	FuIntelThunderboltNvmPrivate *priv = GET_PRIVATE(self);
	g_return_val_if_fail(FU_IS_INTEL_THUNDERBOLT_NVM(self), FALSE);
	return priv->is_host;
}

/**
 * fu_intel_thunderbolt_nvm_is_native:
 * @self: a #FuFirmware
 *
 * Gets if the device is native, i.e. not in recovery mode.
 *
 * Returns: %TRUE if set
 *
 * Since: 1.8.5
 **/
gboolean
fu_intel_thunderbolt_nvm_is_native(FuIntelThunderboltNvm *self)
{
	FuIntelThunderboltNvmPrivate *priv = GET_PRIVATE(self);
	g_return_val_if_fail(FU_IS_INTEL_THUNDERBOLT_NVM(self), FALSE);
	return priv->is_native;
}

/**
 * fu_intel_thunderbolt_nvm_has_pd:
 * @self: a #FuFirmware
 *
 * Gets if the device has power delivery capability.
 *
 * Returns: %TRUE if set
 *
 * Since: 1.8.5
 **/
gboolean
fu_intel_thunderbolt_nvm_has_pd(FuIntelThunderboltNvm *self)
{
	FuIntelThunderboltNvmPrivate *priv = GET_PRIVATE(self);
	g_return_val_if_fail(FU_IS_INTEL_THUNDERBOLT_NVM(self), FALSE);
	return priv->has_pd;
}

/**
 * fu_intel_thunderbolt_nvm_get_model_id:
 * @self: a #FuFirmware
 *
 * Gets the model ID.
 *
 * Returns: an integer, or 0x0 for unset
 *
 * Since: 1.8.5
 **/
guint16
fu_intel_thunderbolt_nvm_get_model_id(FuIntelThunderboltNvm *self)
{
	FuIntelThunderboltNvmPrivate *priv = GET_PRIVATE(self);
	g_return_val_if_fail(FU_IS_INTEL_THUNDERBOLT_NVM(self), 0x0);
	return priv->model_id;
}

/**
 * fu_intel_thunderbolt_nvm_get_flash_size:
 * @self: a #FuFirmware
 *
 * Gets the flash size.
 *
 * NOTE: This does not correspond to a size in bytes, or a power of 2 and is only useful for
 * comparison between firmware and device.
 *
 * Returns: an integer, or 0x0 for unset
 *
 * Since: 1.8.5
 **/
guint8
fu_intel_thunderbolt_nvm_get_flash_size(FuIntelThunderboltNvm *self)
{
	FuIntelThunderboltNvmPrivate *priv = GET_PRIVATE(self);
	g_return_val_if_fail(FU_IS_INTEL_THUNDERBOLT_NVM(self), 0x0);
	return priv->flash_size;
}

static void
fu_intel_thunderbolt_nvm_export(FuFirmware *firmware,
				FuFirmwareExportFlags flags,
				XbBuilderNode *bn)
{
	FuIntelThunderboltNvm *self = FU_INTEL_THUNDERBOLT_NVM(firmware);
	FuIntelThunderboltNvmPrivate *priv = GET_PRIVATE(self);
	fu_xmlb_builder_insert_kx(bn, "vendor_id", priv->vendor_id);
	fu_xmlb_builder_insert_kx(bn, "device_id", priv->device_id);
	fu_xmlb_builder_insert_kx(bn, "model_id", priv->model_id);
	fu_xmlb_builder_insert_kv(bn,
				  "family",
				  fu_intel_thunderbolt_nvm_family_to_string(priv->family));
	fu_xmlb_builder_insert_kb(bn, "is_host", priv->is_host);
	fu_xmlb_builder_insert_kb(bn, "is_native", priv->is_native);
	fu_xmlb_builder_insert_kx(bn, "flash_size", priv->flash_size);
	fu_xmlb_builder_insert_kx(bn, "generation", priv->gen);
	fu_xmlb_builder_insert_kx(bn, "ports", priv->ports);
	fu_xmlb_builder_insert_kb(bn, "has_pd", priv->has_pd);
	for (guint i = 0; i < FU_INTEL_THUNDERBOLT_NVM_SECTION_LAST; i++) {
		if (priv->sections[i] != 0x0) {
			g_autofree gchar *tmp = g_strdup_printf("0x%x", priv->sections[i]);
			g_autoptr(XbBuilderNode) bc =
			    xb_builder_node_insert(bn,
						   "section",
						   "type",
						   fu_intel_thunderbolt_nvm_section_to_string(i),
						   "offset",
						   tmp,
						   NULL);
			g_assert(bc != NULL);
		}
	}
}

static inline gboolean
fu_intel_thunderbolt_nvm_valid_pd_pointer(guint32 pointer)
{
	return pointer != 0 && pointer != 0xFFFFFFFF;
}

static gboolean
fu_intel_thunderbolt_nvm_read_uint8(FuIntelThunderboltNvm *self,
				    FuIntelThunderboltNvmSection section,
				    guint32 offset,
				    guint8 *value,
				    GError **error)
{
	FuIntelThunderboltNvmPrivate *priv = GET_PRIVATE(self);
	g_autoptr(GBytes) fw = NULL;

	/* get blob and read */
	fw = fu_firmware_get_bytes(FU_FIRMWARE(self), error);
	if (fw == NULL)
		return FALSE;
	return fu_memread_uint8_safe(g_bytes_get_data(fw, NULL),
				     g_bytes_get_size(fw),
				     priv->sections[section] + offset,
				     value,
				     error);
}

static gboolean
fu_intel_thunderbolt_nvm_read_uint16(FuIntelThunderboltNvm *self,
				     FuIntelThunderboltNvmSection section,
				     guint32 offset,
				     guint16 *value,
				     GError **error)
{
	FuIntelThunderboltNvmPrivate *priv = GET_PRIVATE(self);
	g_autoptr(GBytes) fw = NULL;

	/* get blob and read */
	fw = fu_firmware_get_bytes(FU_FIRMWARE(self), error);
	if (fw == NULL)
		return FALSE;
	return fu_memread_uint16_safe(g_bytes_get_data(fw, NULL),
				      g_bytes_get_size(fw),
				      priv->sections[section] + offset,
				      value,
				      G_LITTLE_ENDIAN,
				      error);
}

static gboolean
fu_intel_thunderbolt_nvm_read_uint32(FuIntelThunderboltNvm *self,
				     FuIntelThunderboltNvmSection section,
				     guint32 offset,
				     guint32 *value,
				     GError **error)
{
	FuIntelThunderboltNvmPrivate *priv = GET_PRIVATE(self);
	g_autoptr(GBytes) fw = NULL;

	/* get blob and read */
	fw = fu_firmware_get_bytes(FU_FIRMWARE(self), error);
	if (fw == NULL)
		return FALSE;
	return fu_memread_uint32_safe(g_bytes_get_data(fw, NULL),
				      g_bytes_get_size(fw),
				      priv->sections[section] + offset,
				      value,
				      G_LITTLE_ENDIAN,
				      error);
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
fu_intel_thunderbolt_nvm_read_ucode_section_len(FuIntelThunderboltNvm *self,
						guint32 offset,
						guint16 *value,
						GError **error)
{
	if (!fu_intel_thunderbolt_nvm_read_uint16(self,
						  FU_INTEL_THUNDERBOLT_NVM_SECTION_DIGITAL,
						  offset,
						  value,
						  error)) {
		g_prefix_error(error, "failed to read ucode section len: ");
		return FALSE;
	}
	*value *= sizeof(guint32);
	*value += sizeof(guint16);
	return TRUE;
}

/* assumes sections[FU_INTEL_THUNDERBOLT_NVM_SECTION_DIGITAL].offset is already set */
static gboolean
fu_intel_thunderbolt_nvm_read_sections(FuIntelThunderboltNvm *self, GError **error)
{
	guint32 offset;
	FuIntelThunderboltNvmPrivate *priv = GET_PRIVATE(self);

	if (priv->gen >= 3 || priv->gen == 0) {
		if (!fu_intel_thunderbolt_nvm_read_uint32(
			self,
			FU_INTEL_THUNDERBOLT_NVM_SECTION_DIGITAL,
			FU_INTEL_THUNDERBOLT_NVM_DIGITAL_OFFSET_DROM,
			&offset,
			error))
			return FALSE;
		priv->sections[FU_INTEL_THUNDERBOLT_NVM_SECTION_DROM] =
		    offset + priv->sections[FU_INTEL_THUNDERBOLT_NVM_SECTION_DIGITAL];

		if (!fu_intel_thunderbolt_nvm_read_uint32(
			self,
			FU_INTEL_THUNDERBOLT_NVM_SECTION_DIGITAL,
			FU_INTEL_THUNDERBOLT_NVM_DIGITAL_OFFSET_ARC_PARAMS,
			&offset,
			error))
			return FALSE;
		priv->sections[FU_INTEL_THUNDERBOLT_NVM_SECTION_ARC_PARAMS] =
		    offset + priv->sections[FU_INTEL_THUNDERBOLT_NVM_SECTION_DIGITAL];
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

		if (!fu_intel_thunderbolt_nvm_read_uint8(
			self,
			FU_INTEL_THUNDERBOLT_NVM_SECTION_DIGITAL,
			FU_INTEL_THUNDERBOLT_NVM_DIGITAL_OFFSET_AVAILABLE_SECTIONS,
			&available_sections,
			error)) {
			g_prefix_error(error, "failed to read available sections: ");
			return FALSE;
		}
		if (!fu_intel_thunderbolt_nvm_read_uint16(
			self,
			FU_INTEL_THUNDERBOLT_NVM_SECTION_DIGITAL,
			FU_INTEL_THUNDERBOLT_NVM_DIGITAL_OFFSET_UCODE,
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
					    "cannot find needed FW sections in the FW image file");
			return FALSE;
		}

		for (guint8 i = 1; i < DRAM_FLAG; i <<= 1) {
			if (available_sections & i) {
				if (!fu_intel_thunderbolt_nvm_read_ucode_section_len(self,
										     offset,
										     &ucode_offset,
										     error))
					return FALSE;
				offset += ucode_offset;
			}
		}
		priv->sections[FU_INTEL_THUNDERBOLT_NVM_SECTION_DRAM_UCODE] =
		    offset + priv->sections[FU_INTEL_THUNDERBOLT_NVM_SECTION_DIGITAL];
	}

	return TRUE;
}

static gboolean
fu_intel_thunderbolt_nvm_missing_needed_drom(FuIntelThunderboltNvm *self)
{
	FuIntelThunderboltNvmPrivate *priv = GET_PRIVATE(self);
	if (priv->sections[FU_INTEL_THUNDERBOLT_NVM_SECTION_DROM] != 0)
		return FALSE;
	if (priv->is_host && priv->gen < 3)
		return FALSE;
	return TRUE;
}

static gboolean
fu_intel_thunderbolt_nvm_parse(FuFirmware *firmware,
			       GBytes *fw,
			       gsize offset,
			       FwupdInstallFlags flags,
			       GError **error)
{
	FuIntelThunderboltNvm *self = FU_INTEL_THUNDERBOLT_NVM(firmware);
	FuIntelThunderboltNvmPrivate *priv = GET_PRIVATE(self);
	guint8 tmp = 0;
	guint16 version_raw = 0;
	struct {
		guint16 device_id;
		guint gen;
		FuIntelThunderboltNvmFamily family;
		guint ports;
	} hw_info_arr[] = {{0x156D, 2, FU_INTEL_THUNDERBOLT_NVM_FAMILY_FR, 2},	 /* FR 4C */
			   {0x156B, 2, FU_INTEL_THUNDERBOLT_NVM_FAMILY_FR, 1},	 /* FR 2C */
			   {0x157E, 2, FU_INTEL_THUNDERBOLT_NVM_FAMILY_WR, 1},	 /* WR */
			   {0x1578, 3, FU_INTEL_THUNDERBOLT_NVM_FAMILY_AR, 2},	 /* AR 4C */
			   {0x1576, 3, FU_INTEL_THUNDERBOLT_NVM_FAMILY_AR, 1},	 /* AR 2C */
			   {0x15C0, 3, FU_INTEL_THUNDERBOLT_NVM_FAMILY_AR, 1},	 /* AR LP */
			   {0x15D3, 3, FU_INTEL_THUNDERBOLT_NVM_FAMILY_AR_C, 2}, /* AR-C 4C */
			   {0x15DA, 3, FU_INTEL_THUNDERBOLT_NVM_FAMILY_AR_C, 1}, /* AR-C 2C */
			   {0x15E7, 3, FU_INTEL_THUNDERBOLT_NVM_FAMILY_TR, 1},	 /* TR 2C */
			   {0x15EA, 3, FU_INTEL_THUNDERBOLT_NVM_FAMILY_TR, 2},	 /* TR 4C */
			   {0x15EF, 3, FU_INTEL_THUNDERBOLT_NVM_FAMILY_TR, 2},	 /* TR 4C device */
			   {0x15EE, 3, FU_INTEL_THUNDERBOLT_NVM_FAMILY_BB, 0},	 /* BB device */
			   {0x0B26, 4, FU_INTEL_THUNDERBOLT_NVM_FAMILY_GR, 2},	 /* GR USB4 */
			   /* Maple ridge devices
			    * NOTE: These are expected to be flashed via UEFI capsules *not*
			    * Thunderbolt plugin Flashing via fwupd will require matching kernel
			    * work. They're left here only for parsing the binaries
			    */
			   {0x1136, 4, FU_INTEL_THUNDERBOLT_NVM_FAMILY_MR, 2},
			   {0x1137, 4, FU_INTEL_THUNDERBOLT_NVM_FAMILY_MR, 2},
			   {0}};
	g_autofree gchar *version = NULL;
	g_autoptr(FuFirmware) img_payload = NULL;
	g_autoptr(GBytes) fw_payload = NULL;

	/* add this straight away */
	priv->sections[FU_INTEL_THUNDERBOLT_NVM_SECTION_DIGITAL] = offset;

	/* is native */
	if (!fu_intel_thunderbolt_nvm_read_uint8(
		self,
		FU_INTEL_THUNDERBOLT_NVM_SECTION_DIGITAL,
		FU_INTEL_THUNDERBOLT_NVM_DIGITAL_OFFSET_FLAGS_IS_NATIVE,
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
	if (!fu_intel_thunderbolt_nvm_read_uint8(self,
						 FU_INTEL_THUNDERBOLT_NVM_SECTION_DIGITAL,
						 FU_INTEL_THUNDERBOLT_NVM_DIGITAL_OFFSET_FLAGS_HOST,
						 &tmp,
						 error)) {
		g_prefix_error(error, "failed to read is-host: ");
		return FALSE;
	}
	priv->is_host = tmp & (1 << 1);

	/* device ID */
	if (!fu_intel_thunderbolt_nvm_read_uint16(self,
						  FU_INTEL_THUNDERBOLT_NVM_SECTION_DIGITAL,
						  FU_INTEL_THUNDERBOLT_NVM_DIGITAL_OFFSET_DEVICE_ID,
						  &priv->device_id,
						  error)) {
		g_prefix_error(error, "failed to read device-id: ");
		return FALSE;
	}

	/* this is best-effort */
	for (guint i = 0; hw_info_arr[i].device_id != 0; i++) {
		if (hw_info_arr[i].device_id == priv->device_id) {
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
			    "unknown controller: %x",
			    priv->device_id);
		return FALSE;
	}

	/* read sections from file */
	if (!fu_intel_thunderbolt_nvm_read_sections(self, error))
		return FALSE;
	if (fu_intel_thunderbolt_nvm_missing_needed_drom(self)) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_READ,
				    "cannot find required drom section");
		return FALSE;
	}

	/* vendor:model */
	if (priv->sections[FU_INTEL_THUNDERBOLT_NVM_SECTION_DROM] != 0x0) {
		if (!fu_intel_thunderbolt_nvm_read_uint16(
			self,
			FU_INTEL_THUNDERBOLT_NVM_SECTION_DROM,
			FU_INTEL_THUNDERBOLT_NVM_DROM_OFFSET_VENDOR_ID,
			&priv->vendor_id,
			error)) {
			g_prefix_error(error, "failed to read vendor-id: ");
			return FALSE;
		}
		if (!fu_intel_thunderbolt_nvm_read_uint16(
			self,
			FU_INTEL_THUNDERBOLT_NVM_SECTION_DROM,
			FU_INTEL_THUNDERBOLT_NVM_DROM_OFFSET_MODEL_ID,
			&priv->model_id,
			error)) {
			g_prefix_error(error, "failed to read model-id: ");
			return FALSE;
		}
	}

	/* versions */
	switch (priv->family) {
	case FU_INTEL_THUNDERBOLT_NVM_FAMILY_TR:
	case FU_INTEL_THUNDERBOLT_NVM_FAMILY_GR:
		if (!fu_intel_thunderbolt_nvm_read_uint16(
			self,
			FU_INTEL_THUNDERBOLT_NVM_SECTION_DIGITAL,
			FU_INTEL_THUNDERBOLT_NVM_DIGITAL_OFFSET_VERSION,
			&version_raw,
			error)) {
			g_prefix_error(error, "failed to read version: ");
			return FALSE;
		}
		fu_firmware_set_version_raw(FU_FIRMWARE(self), version_raw);
		version = fu_version_from_uint16(version_raw, FWUPD_VERSION_FORMAT_BCD);
		fu_firmware_set_version(FU_FIRMWARE(self), version);
		break;
	default:
		break;
	}

	if (priv->is_host) {
		switch (priv->family) {
		case FU_INTEL_THUNDERBOLT_NVM_FAMILY_AR:
		case FU_INTEL_THUNDERBOLT_NVM_FAMILY_AR_C:
		case FU_INTEL_THUNDERBOLT_NVM_FAMILY_TR:
			/* used for comparison between old and new image, not a raw number */
			if (!fu_intel_thunderbolt_nvm_read_uint8(
				self,
				FU_INTEL_THUNDERBOLT_NVM_SECTION_DIGITAL,
				FU_INTEL_THUNDERBOLT_NVM_DIGITAL_OFFSET_FLASH_SIZE,
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

	/* we're only reading enough to get the vendor-id and model-id */
	if (offset == 0x0 &&
	    g_bytes_get_size(fw) < priv->sections[FU_INTEL_THUNDERBOLT_NVM_SECTION_ARC_PARAMS])
		return TRUE;

	/* has PD */
	if (priv->sections[FU_INTEL_THUNDERBOLT_NVM_SECTION_ARC_PARAMS] != 0x0) {
		guint32 pd_pointer = 0x0;
		if (!fu_intel_thunderbolt_nvm_read_uint32(
			self,
			FU_INTEL_THUNDERBOLT_NVM_SECTION_ARC_PARAMS,
			FU_INTEL_THUNDERBOLT_NVM_ARC_PARAMS_OFFSET_PD_POINTER,
			&pd_pointer,
			error)) {
			g_prefix_error(error, "failed to read pd-pointer: ");
			return FALSE;
		}
		priv->has_pd = fu_intel_thunderbolt_nvm_valid_pd_pointer(pd_pointer);
	}

	/* as as easy-to-grab payload blob */
	if (offset > 0) {
		fw_payload = fu_bytes_new_offset(fw, offset, g_bytes_get_size(fw) - offset, error);
		if (fw_payload == NULL)
			return FALSE;
	} else {
		fw_payload = g_bytes_ref(fw);
	}
	img_payload = fu_firmware_new_from_bytes(fw_payload);
	fu_firmware_set_id(img_payload, FU_FIRMWARE_ID_PAYLOAD);
	fu_firmware_add_image(firmware, img_payload);

	/* success */
	return TRUE;
}

/* can only write version 3 NVM */
static GBytes *
fu_intel_thunderbolt_nvm_write(FuFirmware *firmware, GError **error)
{
	FuIntelThunderboltNvm *self = FU_INTEL_THUNDERBOLT_NVM(firmware);
	FuIntelThunderboltNvmPrivate *priv = GET_PRIVATE(self);
	guint32 digital_size = 0x120;
	guint32 drom_offset = 0 + digital_size;
	guint32 drom_size = 0x20;
	guint32 arc_param_offset = drom_offset + drom_size;
	guint32 arc_param_size = 0x120;
	g_autoptr(GByteArray) buf = g_byte_array_new();

	/* minimal size */
	fu_byte_array_set_size(buf, arc_param_offset + arc_param_size, 0x0);

	/* digital section */
	if (!fu_memwrite_uint8_safe(buf->data,
				    buf->len,
				    FU_INTEL_THUNDERBOLT_NVM_DIGITAL_OFFSET_AVAILABLE_SECTIONS,
				    0x0,
				    error))
		return NULL;
	if (!fu_memwrite_uint16_safe(buf->data,
				     buf->len,
				     FU_INTEL_THUNDERBOLT_NVM_DIGITAL_OFFSET_UCODE,
				     0x0,
				     G_LITTLE_ENDIAN,
				     error))
		return NULL;
	if (!fu_memwrite_uint8_safe(buf->data,
				    buf->len,
				    FU_INTEL_THUNDERBOLT_NVM_DIGITAL_OFFSET_FLAGS_IS_NATIVE,
				    priv->is_native ? 0x20 : 0x0,
				    error))
		return NULL;
	if (!fu_memwrite_uint8_safe(buf->data,
				    buf->len,
				    FU_INTEL_THUNDERBOLT_NVM_DIGITAL_OFFSET_FLAGS_HOST,
				    priv->is_host ? 0x2 : 0x0,
				    error))
		return NULL;
	if (!fu_memwrite_uint32_safe(buf->data,
				     buf->len,
				     FU_INTEL_THUNDERBOLT_NVM_DIGITAL_OFFSET_DEVICE_ID,
				     priv->device_id,
				     G_LITTLE_ENDIAN,
				     error))
		return NULL;
	if (!fu_memwrite_uint16_safe(buf->data,
				     buf->len,
				     FU_INTEL_THUNDERBOLT_NVM_DIGITAL_OFFSET_VERSION,
				     fu_firmware_get_version_raw(firmware),
				     G_LITTLE_ENDIAN,
				     error))
		return NULL;
	if (!fu_memwrite_uint8_safe(buf->data,
				    buf->len,
				    FU_INTEL_THUNDERBOLT_NVM_DIGITAL_OFFSET_FLASH_SIZE,
				    priv->flash_size,
				    error))
		return NULL;

	/* drom section */
	if (!fu_memwrite_uint32_safe(buf->data,
				     buf->len,
				     FU_INTEL_THUNDERBOLT_NVM_DIGITAL_OFFSET_DROM,
				     drom_offset,
				     G_LITTLE_ENDIAN,
				     error))
		return NULL;
	if (!fu_memwrite_uint16_safe(buf->data,
				     buf->len,
				     drom_offset + FU_INTEL_THUNDERBOLT_NVM_DROM_OFFSET_VENDOR_ID,
				     priv->vendor_id,
				     G_LITTLE_ENDIAN,
				     error))
		return NULL;
	if (!fu_memwrite_uint16_safe(buf->data,
				     buf->len,
				     drom_offset + FU_INTEL_THUNDERBOLT_NVM_DROM_OFFSET_MODEL_ID,
				     priv->model_id,
				     G_LITTLE_ENDIAN,
				     error))
		return NULL;

	/* ARC param section */
	if (!fu_memwrite_uint32_safe(buf->data,
				     buf->len,
				     FU_INTEL_THUNDERBOLT_NVM_DIGITAL_OFFSET_ARC_PARAMS,
				     arc_param_offset,
				     G_LITTLE_ENDIAN,
				     error))
		return NULL;
	if (!fu_memwrite_uint32_safe(buf->data,
				     buf->len,
				     arc_param_offset +
					 FU_INTEL_THUNDERBOLT_NVM_ARC_PARAMS_OFFSET_PD_POINTER,
				     priv->has_pd ? 0x1 : 0x0,
				     G_LITTLE_ENDIAN,
				     error))
		return NULL;

	/* success */
	return g_byte_array_free_to_bytes(g_steal_pointer(&buf));
}

static gboolean
fu_intel_thunderbolt_nvm_check_compatible(FuFirmware *firmware,
					  FuFirmware *firmware_other,
					  FwupdInstallFlags flags,
					  GError **error)
{
	FuIntelThunderboltNvm *self = FU_INTEL_THUNDERBOLT_NVM(firmware);
	FuIntelThunderboltNvm *other = FU_INTEL_THUNDERBOLT_NVM(firmware_other);
	FuIntelThunderboltNvmPrivate *priv = GET_PRIVATE(self);
	FuIntelThunderboltNvmPrivate *priv_other = GET_PRIVATE(other);

	if (priv->is_host != priv_other->is_host) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INVALID_FILE,
			    "incorrect firmware mode, got %s, expected %s",
			    priv->is_host ? "host" : "device",
			    priv_other->is_host ? "host" : "device");
		return FALSE;
	}
	if (priv->vendor_id != priv_other->vendor_id) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INVALID_FILE,
			    "incorrect device vendor, got 0x%04x, expected 0x%04x",
			    priv->vendor_id,
			    priv_other->vendor_id);
		return FALSE;
	}
	if (priv->device_id != priv_other->device_id) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INVALID_FILE,
			    "incorrect device type, got 0x%04x, expected 0x%04x",
			    priv->device_id,
			    priv_other->device_id);
		return FALSE;
	}
	if ((flags & FWUPD_INSTALL_FLAG_IGNORE_VID_PID) == 0) {
		if (priv->model_id != priv_other->model_id) {
			g_set_error(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INVALID_FILE,
				    "incorrect device model, got 0x%04x, expected 0x%04x",
				    priv->model_id,
				    priv_other->model_id);
			return FALSE;
		}
		/* old firmware has PD but new doesn't (we don't care about other way around) */
		if (priv->has_pd && !priv_other->has_pd) {
			g_set_error_literal(error,
					    FWUPD_ERROR,
					    FWUPD_ERROR_INVALID_FILE,
					    "incorrect PD section");
			return FALSE;
		}
		if (priv->flash_size != priv_other->flash_size) {
			g_set_error_literal(error,
					    FWUPD_ERROR,
					    FWUPD_ERROR_INVALID_FILE,
					    "incorrect flash size");
			return FALSE;
		}
	}
	return TRUE;
}

static gboolean
fu_intel_thunderbolt_nvm_build(FuFirmware *firmware, XbNode *n, GError **error)
{
	FuIntelThunderboltNvm *self = FU_INTEL_THUNDERBOLT_NVM(firmware);
	FuIntelThunderboltNvmPrivate *priv = GET_PRIVATE(self);
	const gchar *tmp;

	/* simple properties */
	tmp = xb_node_query_text(n, "vendor_id", NULL);
	if (tmp != NULL) {
		guint64 val = 0;
		if (!fu_strtoull(tmp, &val, 0x0, G_MAXUINT16, error))
			return FALSE;
		priv->vendor_id = val;
	}
	tmp = xb_node_query_text(n, "device_id", NULL);
	if (tmp != NULL) {
		guint64 val = 0;
		if (!fu_strtoull(tmp, &val, 0x0, G_MAXUINT16, error))
			return FALSE;
		priv->device_id = val;
	}
	tmp = xb_node_query_text(n, "model_id", NULL);
	if (tmp != NULL) {
		guint64 val = 0;
		if (!fu_strtoull(tmp, &val, 0x0, G_MAXUINT16, error))
			return FALSE;
		priv->model_id = val;
	}
	tmp = xb_node_query_text(n, "family", NULL);
	if (tmp != NULL) {
		priv->family = fu_intel_thunderbolt_nvm_family_from_string(tmp);
		if (priv->family == FU_INTEL_THUNDERBOLT_NVM_FAMILY_UNKNOWN) {
			g_set_error(error,
				    G_IO_ERROR,
				    G_IO_ERROR_INVALID_DATA,
				    "unknown family: %s",
				    tmp);
			return FALSE;
		}
	}
	tmp = xb_node_query_text(n, "flash_size", NULL);
	if (tmp != NULL) {
		guint64 val = 0;
		if (!fu_strtoull(tmp, &val, 0x0, 0x07, error))
			return FALSE;
		priv->flash_size = val;
	}
	tmp = xb_node_query_text(n, "is_host", NULL);
	if (tmp != NULL) {
		if (!fu_strtobool(tmp, &priv->is_host, error))
			return FALSE;
	}
	tmp = xb_node_query_text(n, "is_native", NULL);
	if (tmp != NULL) {
		if (!fu_strtobool(tmp, &priv->is_native, error))
			return FALSE;
	}

	/* success */
	return TRUE;
}

static void
fu_intel_thunderbolt_nvm_init(FuIntelThunderboltNvm *self)
{
	fu_firmware_add_flag(FU_FIRMWARE(self), FU_FIRMWARE_FLAG_HAS_VID_PID);
}

static void
fu_intel_thunderbolt_nvm_class_init(FuIntelThunderboltNvmClass *klass)
{
	FuFirmwareClass *klass_firmware = FU_FIRMWARE_CLASS(klass);
	klass_firmware->export = fu_intel_thunderbolt_nvm_export;
	klass_firmware->parse = fu_intel_thunderbolt_nvm_parse;
	klass_firmware->write = fu_intel_thunderbolt_nvm_write;
	klass_firmware->build = fu_intel_thunderbolt_nvm_build;
	klass_firmware->check_compatible = fu_intel_thunderbolt_nvm_check_compatible;
}

/**
 * fu_intel_thunderbolt_nvm_new:
 *
 * Creates a new #FuFirmware of Intel NVM format
 *
 * Since: 1.8.5
 **/
FuFirmware *
fu_intel_thunderbolt_nvm_new(void)
{
	return FU_FIRMWARE(g_object_new(FU_TYPE_INTEL_THUNDERBOLT_NVM, NULL));
}
