/*
 * Copyright 2018 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include <fwupdplugin.h>

#define FU_TYPE_NVME_DEVICE (fu_nvme_device_get_type())
G_DECLARE_FINAL_TYPE(FuNvmeDevice, fu_nvme_device, FU, NVME_DEVICE, FuPciDevice)

FuNvmeDevice *
fu_nvme_device_new_from_blob(FuContext *ctx, const guint8 *buf, gsize sz, GError **error);
