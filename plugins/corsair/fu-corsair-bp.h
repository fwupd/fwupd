/*
 * Copyright (C) 2021 Andrii Dushko <andrii.dushko@developex.net>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#pragma once

#include <fwupdplugin.h>

#include "fu-corsair-common.h"

#define FU_TYPE_CORSAIR_BP (fu_corsair_bp_get_type())
G_DECLARE_FINAL_TYPE(FuCorsairBp, fu_corsair_bp, FU, CORSAIR_BP, FuUsbDevice)

struct _FuCorsairBpClass {
	FuUsbDeviceClass parent_class;
};

gboolean
fu_corsair_bp_get_property(FuCorsairBp *self,
			   FuCorsairBpProperty property,
			   guint32 *value,
			   GError **error);

gboolean
fu_corsair_bp_activate_firmware(FuCorsairBp *self, FuFirmware *firmware, GError **error);

void
fu_corsair_bp_set_cmd_size(FuCorsairBp *self, guint16 write_size, guint16 read_size);
void
fu_corsair_bp_set_endpoints(FuCorsairBp *self, guint8 epin, guint8 epout);
void
fu_corsair_bp_set_legacy_attach(FuCorsairBp *self, gboolean is_legacy_attach);

FuCorsairBp *
fu_corsair_bp_new(GUsbDevice *usb_device, gboolean is_subdevice);
