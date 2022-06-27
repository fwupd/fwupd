/*
 * Copyright (C) 2021 Xiaotian Cui <xtcui@analogixsemi.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include "fu-analogix-common.h"
#include "fu-analogix-firmware.h"

struct _FuAnalogixFirmware {
	FuIhexFirmwareClass parent_instance;
};

G_DEFINE_TYPE(FuAnalogixFirmware, fu_analogix_firmware, FU_TYPE_IHEX_FIRMWARE)

static gboolean
fu_analogix_firmware_parse(FuFirmware *firmware,
			   GBytes *fw,
			   gsize offset,
			   FwupdInstallFlags flags,
			   GError **error)
{
	FuFirmwareClass *klass = FU_FIRMWARE_CLASS(fu_analogix_firmware_parent_class);
	const guint8 *buf = NULL;
	gsize bufsz = 0;
	guint16 ocm_version;
	guint8 version_hi = 0;
	guint8 version_lo = 0;
	g_autofree gchar *version = NULL;
	g_autoptr(FuFirmware) fw_ocm = NULL;
	g_autoptr(GBytes) blob_cus = NULL;
	g_autoptr(GBytes) blob = NULL;
	g_autoptr(GBytes) blob_ocm = NULL;
	g_autoptr(GBytes) blob_srx = NULL;
	g_autoptr(GBytes) blob_stx = NULL;

	/* convert to binary with FuIhexFirmware->parse */
	if (!klass->parse(firmware, fw, offset, flags, error))
		return FALSE;
	blob = fu_firmware_get_bytes_with_patches(firmware, error);
	if (blob == NULL)
		return FALSE;

	/* OCM section only, CUSTOM section only, or multiple sections excluded CUSTOM */
	if (g_bytes_get_size(blob) == OCM_FLASH_SIZE) {
		blob_ocm = g_bytes_ref(blob);
	} else if (g_bytes_get_size(blob) == CUSTOM_FLASH_SIZE) {
		/* custom */
		blob_cus = fu_bytes_new_offset(blob, 0, CUSTOM_FLASH_SIZE, error);
	} else {
		blob_ocm = fu_bytes_new_offset(blob, 0, OCM_FLASH_SIZE, error);
		if (blob_ocm == NULL)
			return FALSE;
	}
	if (blob_ocm != NULL) {
		fw_ocm = fu_firmware_new_from_bytes(blob_ocm);
		fu_firmware_set_id(fw_ocm, "ocm");
		fu_firmware_set_addr(fw_ocm, FLASH_OCM_ADDR);
		fu_firmware_add_image(firmware, fw_ocm);

		/* get OCM version */
		buf = g_bytes_get_data(blob_ocm, &bufsz);
		if (!fu_memread_uint8_safe(buf,
					   bufsz,
					   OCM_FW_VERSION_ADDR - FLASH_OCM_ADDR + 8,
					   &version_hi,
					   error))
			return FALSE;
		if (!fu_memread_uint8_safe(buf,
					   bufsz,
					   OCM_FW_VERSION_ADDR - FLASH_OCM_ADDR + 12,
					   &version_lo,
					   error))
			return FALSE;
		ocm_version = ((guint16)version_hi) << 8 | version_lo;
		fu_firmware_set_version_raw(fw_ocm, ocm_version);
		version = g_strdup_printf("%02x.%02x", version_hi, version_lo);
		fu_firmware_set_version(fw_ocm, version);
	}

	/* TXFW is optional */
	blob_stx =
	    fu_bytes_new_offset(blob, FLASH_TXFW_ADDR - FLASH_OCM_ADDR, SECURE_OCM_TX_SIZE, NULL);
	if (blob_stx != NULL && !fu_bytes_is_empty(blob_stx)) {
		g_autoptr(FuFirmware) fw2 = fu_firmware_new_from_bytes(blob_stx);
		fu_firmware_set_id(fw2, "stx");
		fu_firmware_set_addr(fw2, FLASH_TXFW_ADDR);
		fu_firmware_add_image(firmware, fw2);
	}

	/* RXFW is optional */
	blob_srx =
	    fu_bytes_new_offset(blob, FLASH_RXFW_ADDR - FLASH_OCM_ADDR, SECURE_OCM_RX_SIZE, NULL);
	if (blob_srx != NULL && !fu_bytes_is_empty(blob_srx)) {
		g_autoptr(FuFirmware) fw2 = fu_firmware_new_from_bytes(blob_srx);
		fu_firmware_set_id(fw2, "srx");
		fu_firmware_set_addr(fw2, FLASH_RXFW_ADDR);
		fu_firmware_add_image(firmware, fw2);
	}
	if (blob_cus != NULL && !fu_bytes_is_empty(blob_cus)) {
		g_autoptr(FuFirmware) fw2 = fu_firmware_new_from_bytes(blob_cus);
		fu_firmware_set_id(fw2, "custom");
		fu_firmware_set_addr(fw2, FLASH_CUSTOM_ADDR);
		fu_firmware_add_image(firmware, fw2);
	}

	/* success */
	return TRUE;
}

static void
fu_analogix_firmware_init(FuAnalogixFirmware *self)
{
	fu_ihex_firmware_set_padding_value(FU_IHEX_FIRMWARE(self), 0xFF);
}

static void
fu_analogix_firmware_class_init(FuAnalogixFirmwareClass *klass)
{
	FuFirmwareClass *klass_firmware = FU_FIRMWARE_CLASS(klass);
	klass_firmware->parse = fu_analogix_firmware_parse;
}

FuFirmware *
fu_analogix_firmware_new(void)
{
	return FU_FIRMWARE(g_object_new(FU_TYPE_ANALOGIX_FIRMWARE, NULL));
}
