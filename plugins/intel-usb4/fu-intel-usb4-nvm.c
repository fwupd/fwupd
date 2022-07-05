/*
 * Copyright (C) 2022 Richard Hughes <richard@hughsie.com>
 * Copyright (C) 2021 Intel Corporation.
 * Copyright (C) 2021 Dell Inc.
 * All rights reserved.
 *
 * This software and associated documentation (if any) is furnished
 * under a license and may only be used or copied in accordance
 * with the terms of the license.
 *
 * This file is provided under a dual MIT/LGPLv2 license.  When using or
 * redistributing this file, you may do so under either license.
 * Dell Chooses the MIT license part of Dual MIT/LGPLv2 license agreement.
 *
 * SPDX-License-Identifier: LGPL-2.1+ OR MIT
 */

#include "config.h"

#include <fwupdplugin.h>

#include "fu-intel-usb4-firmware.h"

typedef struct {
	guint16 vendor_id;
	guint16 product_id;
} FuIntelUsb4NvmPrivate;

G_DEFINE_TYPE_WITH_PRIVATE(FuIntelUsb4Nvm, fu_intel_usb4_nvm, FU_TYPE_FIRMWARE)
#define GET_PRIVATE(o) (fu_intel_usb4_nvm_get_instance_private(o))

/* NVM offset */
#define NVM_VER_OFFSET_MAJOR 0xa
#define NVM_VER_OFFSET_MINOR 0x9
#define NVM_VID_OFFSET_MAJOR 0x221
#define NVM_VID_OFFSET_MINOR 0x220
#define NVM_PID_OFFSET_MAJOR 0x223
#define NVM_PID_OFFSET_MINOR 0x222

static gboolean
fu_intel_usb4_nvm_parse(FuFirmware *firmware,
			GBytes *fw,
			gsize offset,
			FwupdInstallFlags flags,
			GError **error)
{
	FuIntelUsb4Nvm *self = FU_INTEL_USB4_NVM(firmware);
	FuIntelUsb4NvmPrivate *priv = GET_PRIVATE(self);
	guint16 version_raw = 0x0;
	g_autofree gchar *version = NULL;
	g_autoptr(FuFirmware) img_payload = NULL;
	g_autoptr(GBytes) fw_payload = NULL;

	/* vid:pid */
	if (!fu_memread_uint16_safe(g_bytes_get_data(fw, NULL),
				    g_bytes_get_size(fw),
				    offset + NVM_VID_OFFSET_MINOR,
				    &priv->vendor_id,
				    G_LITTLE_ENDIAN,
				    error))
		return FALSE;
	if (!fu_memread_uint16_safe(g_bytes_get_data(fw, NULL),
				    g_bytes_get_size(fw),
				    offset + NVM_PID_OFFSET_MINOR,
				    &priv->product_id,
				    G_LITTLE_ENDIAN,
				    error))
		return FALSE;

	/* version */
	if (!fu_memread_uint16_safe(g_bytes_get_data(fw, NULL),
				    g_bytes_get_size(fw),
				    offset + NVM_VER_OFFSET_MINOR,
				    &version_raw,
				    G_LITTLE_ENDIAN,
				    error))
		return FALSE;
	version = g_strdup_printf("%02x.%02x", version_raw >> 8, (guint)version_raw & 0xFF);
	fu_firmware_set_version_raw(firmware, version_raw);
	fu_firmware_set_version(firmware, version);

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

static void
fu_intel_usb4_nvm_export(FuFirmware *firmware, FuFirmwareExportFlags flags, XbBuilderNode *bn)
{
	FuIntelUsb4Nvm *self = FU_INTEL_USB4_NVM(firmware);
	FuIntelUsb4NvmPrivate *priv = GET_PRIVATE(self);
	fu_xmlb_builder_insert_kx(bn, "vendor_id", priv->vendor_id);
	fu_xmlb_builder_insert_kx(bn, "product_id", priv->product_id);
}

guint16
fu_intel_usb4_nvm_get_vendor_id(FuIntelUsb4Nvm *self)
{
	FuIntelUsb4NvmPrivate *priv = GET_PRIVATE(self);
	return priv->vendor_id;
}

guint16
fu_intel_usb4_nvm_get_product_id(FuIntelUsb4Nvm *self)
{
	FuIntelUsb4NvmPrivate *priv = GET_PRIVATE(self);
	return priv->product_id;
}

static void
fu_intel_usb4_nvm_init(FuIntelUsb4Nvm *self)
{
	fu_firmware_add_flag(FU_FIRMWARE(self), FU_FIRMWARE_FLAG_HAS_VID_PID);
}

static void
fu_intel_usb4_nvm_class_init(FuIntelUsb4NvmClass *klass)
{
	FuFirmwareClass *klass_firmware = FU_FIRMWARE_CLASS(klass);
	klass_firmware->parse = fu_intel_usb4_nvm_parse;
	klass_firmware->export = fu_intel_usb4_nvm_export;
}

FuFirmware *
fu_intel_usb4_nvm_new(void)
{
	return FU_FIRMWARE(g_object_new(FU_TYPE_INTEL_USB4_NVM, NULL));
}
