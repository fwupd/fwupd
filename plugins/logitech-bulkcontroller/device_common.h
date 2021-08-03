//
//  device_common.h
//  ProtocDemo
//
//  Created by Edwin Estrada on 7/29/21.
//  Copyright © 2021 Edwin Estrada. All rights reserved.
//

#ifndef device_common_h
#define device_common_h

#include <glib.h>

typedef enum  {
  kDeviceStateUnknown = -1,
  kDeviceStateOffline,
  kDeviceStateOnline,
  kDeviceStateIdle,
  kDeviceStateInUse,
  kDeviceStateAudioOnly,
  kDeviceStateEnumerating
} device_state;

typedef enum  {
  kUpdateStateUnknown = -1,
  kUpdateStateCurrent,
  kUpdateStateAvailable,
  kUpdateStateStarting = 3,
  kUpdateStateDownloading,
  kUpdateStateReady,
  kUpdateStateUpdating,
  kUpdateStateScheduled,
  kUpdateStateError
} update_state;

typedef struct _device_info {
  gchar type[25];
  device_state status;
  update_state update_status;
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
  gchar zoom_focus_version[10];
  gchar zoom_focus_hw[10];
  gchar house_keeping_version[10];
  gchar house_keeping_hw[25];
  gchar audio_version[10];
  gchar audio_hw[10];
} device_info;


#endif /* device_common_h */
