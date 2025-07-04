/*
 * Copyright 2023 Adam.Chen <Adam.Chen@genesyslogic.com.tw>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include <fwupdplugin.h>

#define FU_TYPE_GENESYS_HUBHID_DEVICE (fu_genesys_hubhid_device_get_type())
G_DECLARE_FINAL_TYPE(FuGenesysHubhidDevice,
		     fu_genesys_hubhid_device,
		     FU,
		     GENESYS_HUBHID_DEVICE,
		     FuHidDevice)

gboolean
fu_genesys_hubhid_device_send_report(FuGenesysHubhidDevice *self,
				     FuProgress *progress,
				     FuUsbDirection direction,
				     FuUsbRequestType request_type,
				     FuUsbRecipient recipient,
				     guint8 request,
				     guint16 value,
				     guint16 idx,
				     guint8 *data,
				     gsize datasz,
				     GError **error);
