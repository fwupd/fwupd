/*
 * Copyright (C) 2019 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#pragma once

#include <fwupdplugin.h>

#define FU_TYPE_TPM_DEVICE (fu_tpm_device_get_type())
G_DECLARE_DERIVABLE_TYPE(FuTpmDevice, fu_tpm_device, FU, TPM_DEVICE, FuUdevDevice)

struct _FuTpmDeviceClass {
	FuDeviceClass parent_class;
};

void
fu_tpm_device_set_family(FuTpmDevice *self, const gchar *family);
const gchar *
fu_tpm_device_get_family(FuTpmDevice *self);
void
fu_tpm_device_add_checksum(FuTpmDevice *self, guint idx, const gchar *checksum);
GPtrArray *
fu_tpm_device_get_checksums(FuTpmDevice *self, guint idx);
