/*
 * Copyright (C) 2021 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#pragma once

#include "fu-udev-device.h"

#define FU_TYPE_MEI_DEVICE (fu_mei_device_get_type())
G_DECLARE_DERIVABLE_TYPE(FuMeiDevice, fu_mei_device, FU, MEI_DEVICE, FuUdevDevice)

struct _FuMeiDeviceClass {
	FuUdevDeviceClass parent_class;
	gpointer __reserved[31];
};

/**
 * FU_MEI_DEVICE_HECI_AMTHI_GUID:
 *
 * GUID used to connect to the PTHI client (via the HECI device).
 */
#define FU_MEI_DEVICE_HECI_AMTHI_GUID "12f80028-b4b7-4b2d-aca8-46e0ff65814c"

/**
 * FU_MEI_DEVICE_HECI_WATCHDOG_GUID:
 *
 * GUID used to connect to the Watchdog client (via the HECI device).
 */
#define FU_MEI_DEVICE_HECI_WATCHDOG_GUID "05b79a6f-4628-4d7f-899d-a91514cb32ab"

/**
 * FU_MEI_DEVICE_HECI_FWUPDATE_GUID:
 *
 * GUID used to connect to the FWUpdate client (via the HECI device).
 */
#define FU_MEI_DEVICE_HECI_FWUPDATE_GUID "309dcde8-ccb1-4062-8f78-600115a34327"

/**
 * FU_MEI_DEVICE_HECI_MKHI_GUID:
 *
 * GUID used to connect to the MKHI client (via the HECI device).
 */
#define FU_MEI_DEVICE_HECI_MKHI_GUID "8e6a6715-9abc-4043-88ef-9e39c6f63e0f"

/**
 * FU_MEI_DEVICE_HECI_UPI_GUID:
 *
 * GUID used to connect to the Unique Platform ID client (via the HECI device).
 */
#define FU_MEI_DEVICE_HECI_UPI_GUID "92136c79-5fea-4cfd-980e-23be07fa5e9f"

/**
 * FU_MEI_DEVICE_HECI_PSR_GUID:
 *
 * GUID used to connect to the Platform Service Record client (via the HECI device).
 */
#define FU_MEI_DEVICE_HECI_PSR_GUID "ed6703fa-d312-4e8b-9ddd-2155bb2dee65"

gboolean
fu_mei_device_connect(FuMeiDevice *self,
		      const gchar *guid,
		      guchar req_protocol_version,
		      GError **error);
gboolean
fu_mei_device_read(FuMeiDevice *self,
		   guint8 *buf,
		   gsize bufsz,
		   gsize *bytes_read,
		   guint timeout_ms,
		   GError **error);
gboolean
fu_mei_device_write(FuMeiDevice *self,
		    const guint8 *buf,
		    gsize bufsz,
		    guint timeout_ms,
		    GError **error);
guint
fu_mei_device_get_max_msg_length(FuMeiDevice *self);
guint8
fu_mei_device_get_protocol_version(FuMeiDevice *self);
