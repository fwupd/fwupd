/*
 * Copyright 1999-2021 Logitech, Inc.
 * All Rights Reserved
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include <glib.h>

#include "usb_msg.pb-c.h"

#define SET_TIME_DELAY_MS 500 /* send future time to keep PC & device time as close as possible */

typedef enum {
	kProtoId_UnknownId,
	kProtoId_GetDeviceInfoResponse,
	kProtoId_TransitionToDeviceModeResponse,
	kProtoId_Ack,
	kProtoId_KongEvent,
	kProtoId_HandshakeEvent,
	kProtoId_CrashDumpAvailableEvent
} FuLogitechBulkcontrollerProtoId;

GByteArray *
fu_logitech_bulkcontroller_proto_manager_generate_get_device_info_request(FuDevice *device)
    G_GNUC_NON_NULL(1);
GByteArray *
fu_logitech_bulkcontroller_proto_manager_generate_transition_to_device_mode_request(
    FuDevice *device) G_GNUC_NON_NULL(1);
GByteArray *
fu_logitech_bulkcontroller_proto_manager_generate_set_device_time_request(FuDevice *device,
									  GError **error)
    G_GNUC_NON_NULL(1);
GByteArray *
fu_logitech_bulkcontroller_proto_manager_decode_message(const guint8 *data,
							guint32 len,
							FuLogitechBulkcontrollerProtoId *proto_id,
							GError **error) G_GNUC_NON_NULL(1, 3);
