/*
 * Copyright (C) 2022 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#pragma once

#include <fwupdplugin.h>

#define FU_TYPE_INTEL_ME_HECI_DEVICE (fu_intel_me_heci_device_get_type())
G_DECLARE_DERIVABLE_TYPE(FuIntelMeHeciDevice,
			 fu_intel_me_heci_device,
			 FU,
			 INTEL_ME_HECI_DEVICE,
			 FuMeiDevice)

struct _FuIntelMeHeciDeviceClass {
	FuMeiDeviceClass parent_class;
};

#define FU_INTEL_ME_HECI_DEVICE_FLAG_LEAKED_KM (1 << 0)

GByteArray *
fu_intel_me_heci_device_read_file(FuIntelMeHeciDevice *self, const gchar *filename, GError **error);
GByteArray *
fu_intel_me_heci_device_read_file_ex(FuIntelMeHeciDevice *self,
				     guint32 file_id,
				     guint32 section,
				     guint32 datasz_req,
				     GError **error);
