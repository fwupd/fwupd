/*
 * Copyright 2026 NVIDIA Corporation
 * Author: Vishnu Raghav <vraghav@nvidia.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include <fwupdplugin.h>

#include "fu-nvidia-oob-request.h"

#define FU_TYPE_NVIDIA_OOB_BMC (fu_nvidia_oob_bmc_get_type())
G_DECLARE_FINAL_TYPE(FuNvidiaOobBmc, fu_nvidia_oob_bmc, FU, NVIDIA_OOB_BMC, GObject)

FuNvidiaOobBmc *
fu_nvidia_oob_bmc_new(void);
gboolean
fu_nvidia_oob_bmc_set_uri(FuNvidiaOobBmc *self, const gchar *uri, GError **error);
void
fu_nvidia_oob_bmc_set_cacheck(FuNvidiaOobBmc *self, gboolean cacheck);
void
fu_nvidia_oob_bmc_set_credentials(FuNvidiaOobBmc *self,
				  const gchar *username,
				  const gchar *password);
void
fu_nvidia_oob_bmc_set_session_key_file(FuNvidiaOobBmc *self, const gchar *session_key_file);
FuNvidiaOobRequest *
fu_nvidia_oob_bmc_request_new(FuNvidiaOobBmc *self);
gboolean
fu_nvidia_oob_bmc_setup(FuNvidiaOobBmc *self, gchar **inventory_uri, GError **error);
const gchar *
fu_nvidia_oob_bmc_get_push_uri(FuNvidiaOobBmc *self);
guint64
fu_nvidia_oob_bmc_get_max_image_size(FuNvidiaOobBmc *self);
const gchar *
fu_nvidia_oob_bmc_get_uploaded_checksum(FuNvidiaOobBmc *self);
void
fu_nvidia_oob_bmc_set_uploaded_checksum(FuNvidiaOobBmc *self, const gchar *checksum);
gboolean
fu_nvidia_oob_bmc_get_composite(FuNvidiaOobBmc *self);
void
fu_nvidia_oob_bmc_set_composite(FuNvidiaOobBmc *self, gboolean composite);
FuDevice *
fu_nvidia_oob_bmc_get_written_device(FuNvidiaOobBmc *self);
void
fu_nvidia_oob_bmc_set_written_device(FuNvidiaOobBmc *self, FuDevice *device);
void
fu_nvidia_oob_bmc_add_device(FuNvidiaOobBmc *self, FuDevice *device);
GPtrArray *
fu_nvidia_oob_bmc_get_devices(FuNvidiaOobBmc *self);
void
fu_nvidia_oob_bmc_remove_devices(FuNvidiaOobBmc *self);
