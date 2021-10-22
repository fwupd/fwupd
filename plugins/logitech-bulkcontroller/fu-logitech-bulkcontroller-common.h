/*
 * Copyright (c) 1999-2021 Logitech, Inc.
 * All Rights Reserved
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#pragma once

#include <glib.h>

#include "usb_msg.pb-c.h"

typedef enum {
	kDeviceStateUnknown = -1,
	kDeviceStateOffline,
	kDeviceStateOnline,
	kDeviceStateIdle,
	kDeviceStateInUse,
	kDeviceStateAudioOnly,
	kDeviceStateEnumerating
} FuLogitechBulkcontrollerDeviceStatus;

typedef enum {
	kUpdateStateUnknown = -1,
	kUpdateStateCurrent,
	kUpdateStateAvailable,
	kUpdateStateStarting = 3,
	kUpdateStateDownloading,
	kUpdateStateReady,
	kUpdateStateUpdating,
	kUpdateStateScheduled,
	kUpdateStateError
} FuLogitechBulkcontrollerDeviceUpdateState;

typedef enum {
	kProtoId_UnknownId,
	kProtoId_GetDeviceInfoResponse,
	kProtoId_TransitionToDeviceModeResponse,
	kProtoId_Ack,
	kProtoId_KongEvent,
	kProtoId_HandshakeEvent,
	kProtoId_CrashDumpAvailableEvent
} FuLogitechBulkcontrollerProtoId;

const gchar *
fu_logitech_bulkcontroller_device_status_to_string(FuLogitechBulkcontrollerDeviceStatus status);
const gchar *
fu_logitech_bulkcontroller_device_update_state_to_string(
    FuLogitechBulkcontrollerDeviceUpdateState update_state);
GByteArray *
proto_manager_generate_get_device_info_request(void);
GByteArray *
proto_manager_generate_transition_to_device_mode_request(void);
GByteArray *
proto_manager_generate_set_device_time_request(void);
GByteArray *
proto_manager_decode_message(const guint8 *data,
			     guint32 len,
			     FuLogitechBulkcontrollerProtoId *proto_id,
			     GError **error);
