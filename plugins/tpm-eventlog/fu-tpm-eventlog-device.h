/*
 * Copyright (C) 2019 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#pragma once

#include "fu-plugin.h"

#define FU_TYPE_TPM_EVENTLOG_DEVICE (fu_tpm_eventlog_device_get_type ())
G_DECLARE_FINAL_TYPE (FuTpmEventlogDevice, fu_tpm_eventlog_device, FU, TPM_EVENTLOG_DEVICE, FuDevice)

FuTpmEventlogDevice *fu_tpm_eventlog_device_new		(const guint8	*buf,
							 gsize		 bufsz,
							 GError		**error);
gchar		*fu_tpm_eventlog_device_report_metadata	(FuTpmEventlogDevice *self);
GPtrArray	*fu_tpm_eventlog_device_get_checksums	(FuTpmEventlogDevice *self,
							 guint8		 pcr,
							 GError		**error);
