/*
 * Copyright 2026 Yuchao Li <liyc44@lenovo.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include "fu-lenovo-accessory-impl.h"

/* HID feature report
 *
 * Commands are carried as HID Feature reports over the interface-3 control
 * endpoint (EP0). The control-transfer data buffer does NOT include the
 * report-id byte (unlike the hidraw ioctl path), so the on-wire frame is a
 * flat 64 bytes starting at the command header. The report-id (0x00) is
 * passed out-of-band through the SET_REPORT/GET_REPORT wValue low byte. */
#define FU_LENOVO_ACCESSORY_HID_REPORT_ID 0x00
#define FU_LENOVO_ACCESSORY_HID_BUFSZ	  64
#define FU_LENOVO_ACCESSORY_HID_TIMEOUT	  1000 /* ms */

/* USB interface assignment (stable across firmware revisions) */
#define FU_LENOVO_ACCESSORY_IFACE_CMD 0x03 /* vendor HID, Feature report */

/* the bootloader exposes a single HID interface, so commands go to iface 0 */
#define FU_LENOVO_ACCESSORY_IFACE_BL 0x00

GByteArray *
fu_lenovo_accessory_hid_read(FuLenovoAccessoryImpl *impl, GError **error) G_GNUC_NON_NULL(1);
gboolean
fu_lenovo_accessory_hid_write(FuLenovoAccessoryImpl *impl, GByteArray *buf, GError **error)
    G_GNUC_NON_NULL(1, 2);
GByteArray *
fu_lenovo_accessory_hid_process(FuLenovoAccessoryImpl *impl, GByteArray *buf, GError **error)
    G_GNUC_NON_NULL(1, 2);

gboolean
fu_lenovo_accessory_hid_add_children(FuLenovoAccessoryImpl *self, GError **error)
    G_GNUC_NON_NULL(1);
