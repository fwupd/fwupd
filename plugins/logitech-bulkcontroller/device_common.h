/*
 * Copyright (c) 1999-2021 Logitech, Inc.
 * All Rights Reserved
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#ifndef device_common_h
#define device_common_h

#include <glib.h>

typedef enum { kDeviceTypeUnknown, kDeviceTypeRallyBar, kDeviceTypeRallyBarMini } DeviceType;

typedef enum {
	kDeviceStateUnknown = -1,
	kDeviceStateOffline,
	kDeviceStateOnline,
	kDeviceStateIdle,
	kDeviceStateInUse,
	kDeviceStateAudioOnly,
	kDeviceStateEnumerating
} DeviceState;

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
} UpdateState;

typedef struct _DeviceInfo {
	gchar type[25];
	DeviceType device_type;
	DeviceState status;
	UpdateState update_status;
	gint update_progress;
	gchar update_error_code[30];
	gchar name[25];
	gchar sw[25];
	gchar manifest[25];
	gchar os[25];
	gchar osv[25];
	gchar serial[25];
	gchar build_type[25];
	gchar hw[10];
	gchar pan_tilt_version[10];
	gchar pan_tilt_hw[10];
	// RallyBar mini does not have zoom and focus
	gchar zoom_focus_version[10];
	gchar zoom_focus_hw[10];
	gchar house_keeping_version[10];
	gchar house_keeping_hw[25];
	gchar audio_version[10];
	gchar audio_hw[10];
} DeviceInfo;

#endif /* device_common_h */
