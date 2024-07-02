/*
 * Copyright 2024 Dell Inc.
 * All rights reserved.
 *
 * This software and associated documentation (if any) is furnished
 * under a license and may only be used or copied in accordance
 * with the terms of the license.
 *
 * This file is provided under a dual MIT/LGPLv2 license.  When using or
 * redistributing this file, you may do so under either license.
 * Dell Chooses the MIT license part of Dual MIT/LGPLv2 license agreement.
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later OR MIT
 */

#include "config.h"

#include <errno.h>
#include <string.h>

#include "fu-dell-dock-hid-v2.h"

#define HIDv2_TRANSACTION_TIMEOUT 2000

typedef struct __attribute__((packed)) {
	/* header */
	guint8 cmd;
	guint8 ext;
	guint32 data_sz;

	/* data */
	struct FuHidv2FwupdateDevFwInfo {
		guint8 subcmd;
		guint8 dev_type;
		guint8 dev_identifier;
		guint32 fw_sz;
	} __attribute__((packed)) dev_fw_info;
	guint8 *fw_data;
} FuHidv2FwupPkg;

gboolean
fu_dell_dock_hid_v2_write(FuDevice *device, GBytes *buf, GError **error)
{
	guint8 *data = (guint8 *)g_bytes_get_data(buf, NULL);
	gsize data_sz = g_bytes_get_size(buf);

	return fu_hid_device_set_report(FU_HID_DEVICE(device),
					0x0,
					data,
					data_sz,
					HIDv2_TRANSACTION_TIMEOUT,
					FU_HID_DEVICE_FLAG_RETRY_FAILURE,
					error);
}

gboolean
fu_dell_dock_hid_v2_read(FuDevice *device, GByteArray *res, GError **error)
{
	return fu_hid_device_get_report(FU_HID_DEVICE(device),
					0x0,
					res->data,
					res->len,
					HIDv2_TRANSACTION_TIMEOUT,
					FU_HID_DEVICE_FLAG_RETRY_FAILURE,
					error);
}

GBytes *
fu_dell_dock_hid_v2_fwup_pkg_new(GBytes *fw, guint8 dev_type, guint8 dev_identifier)
{
	g_autoptr(GByteArray) fwbuf = g_byte_array_new();
	gsize fw_size = g_bytes_get_size(fw);

	/* header */
	fu_byte_array_append_uint8(fwbuf, HID_v2_CMD_WRITE_DATA);
	fu_byte_array_append_uint8(fwbuf, HID_v2_EXT_WRITE_DATA);
	fu_byte_array_append_uint32(fwbuf, 7 + fw_size, G_BIG_ENDIAN); // 7 = sizeof(command)

	/* command */
	fu_byte_array_append_uint8(fwbuf, HID_v2_SUBCMD_FWUPDATE);
	fu_byte_array_append_uint8(fwbuf, dev_type);
	fu_byte_array_append_uint8(fwbuf, dev_identifier);
	fu_byte_array_append_uint32(fwbuf, fw_size, G_BIG_ENDIAN);

	/* data */
	fu_byte_array_append_bytes(fwbuf, fw);

	return g_bytes_new(fwbuf->data, fwbuf->len);
}
