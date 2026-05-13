/*
 * Copyright 2020 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include "fu-elantp-struct.h"

#define FU_TYPE_ELANTP_HID_DEVICE (fu_elantp_hid_device_get_type())
G_DECLARE_FINAL_TYPE(FuElantpHidDevice, fu_elantp_hid_device, FU, ELANTP_HID_DEVICE, FuHidrawDevice)

#define FU_ELANTP_DEVICE_PRIVATE_FLAG_CAN_QUERY_HAPTIC_FUNCTION "can-query-haptic-function"

gboolean
fu_elantp_hid_device_write_cmd(FuElantpHidDevice *self,
			       FuEtpRptid report_id,
			       guint16 reg,
			       FuEtpCmd cmd,
			       GError **error);
gboolean
fu_elantp_hid_device_read_cmd(FuElantpHidDevice *self,
			      FuEtpRptid report_id,
			      guint16 reg,
			      guint16 *value,
			      GError **error);
