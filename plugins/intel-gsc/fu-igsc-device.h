/*
 * Copyright (C) 2022 Intel, Inc.
 * Copyright (C) 2022 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+ OR Apache-2.0
 */

#pragma once

#include <fwupdplugin.h>

#include "fu-igsc-heci.h"

#define FU_TYPE_IGSC_DEVICE (fu_igsc_device_get_type())
G_DECLARE_FINAL_TYPE(FuIgscDevice, fu_igsc_device, FU, IGSC_DEVICE, FuMeiDevice)

gboolean
fu_igsc_device_get_oprom_code_devid_enforcement(FuIgscDevice *self);

guint16
fu_igsc_device_get_ssvid(FuIgscDevice *self);
guint16
fu_igsc_device_get_ssdid(FuIgscDevice *self);

gboolean
fu_igsc_device_write_blob(FuIgscDevice *self,
			  enum gsc_fwu_heci_payload_type payload_type,
			  GBytes *fw_info,
			  GBytes *fw_payload,
			  FuProgress *progress,
			  GError **error);

gboolean
fu_igsc_device_get_aux_version(FuIgscDevice *self,
			       guint32 *oem_version,
			       guint16 *major_version,
			       guint16 *major_vcn,
			       GError **error);
gboolean
fu_igsc_device_get_version_raw(FuIgscDevice *self,
			       enum gsc_fwu_heci_partition_version partition,
			       guint8 *buf,
			       gsize bufsz,
			       GError **error);
