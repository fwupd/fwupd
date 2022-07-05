/*
 * Copyright (C) 2022 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#pragma once

#include <fwupdplugin.h>

#define FU_TYPE_INTEL_USB4_NVM (fu_intel_usb4_nvm_get_type())
G_DECLARE_DERIVABLE_TYPE(FuIntelUsb4Nvm, fu_intel_usb4_nvm, FU, INTEL_USB4_NVM, FuFirmware)

struct _FuIntelUsb4NvmClass {
	FuFirmwareClass parent_class;
};

FuFirmware *
fu_intel_usb4_nvm_new(void);
guint16
fu_intel_usb4_nvm_get_vendor_id(FuIntelUsb4Nvm *self);
guint16
fu_intel_usb4_nvm_get_product_id(FuIntelUsb4Nvm *self);
