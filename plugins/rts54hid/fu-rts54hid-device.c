/*
 * Copyright 2018 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include "fu-rts54hid-common.h"
#include "fu-rts54hid-device.h"
#include "fu-rts54hid-struct.h"

G_DEFINE_TYPE(FuRts54hidDevice, fu_rts54hid_device, FU_TYPE_HID_DEVICE)

gboolean
fu_rts54hid_device_set_report(FuRts54hidDevice *self, GByteArray *buf, GError **error)
{
	return fu_hid_device_set_report(FU_HID_DEVICE(self),
					0x0,
					buf->data,
					buf->len,
					FU_RTS54HID_DEVICE_TIMEOUT * 2,
					FU_HID_DEVICE_FLAG_NONE,
					error);
}

gboolean
fu_rts54hid_device_get_report(FuRts54hidDevice *self, GByteArray *buf, GError **error)
{
	return fu_hid_device_set_report(FU_HID_DEVICE(self),
					0x0,
					buf->data,
					buf->len,
					FU_RTS54HID_DEVICE_TIMEOUT,
					FU_HID_DEVICE_FLAG_NONE,
					error);
}

gboolean
fu_rts54hid_device_write_chunk(FuRts54hidDevice *self, FuChunk *chk, GError **error)
{
	g_autoptr(FuRts54hidCmdBuffer) st = fu_rts54hid_cmd_buffer_new();

	fu_rts54hid_cmd_buffer_set_cmd(st, FU_RTS54HID_CMD_WRITE_DATA);
	fu_rts54hid_cmd_buffer_set_ext(st, FU_RTS54HID_EXT_WRITEFLASH);
	fu_rts54hid_cmd_buffer_set_dwregaddr(st, fu_chunk_get_address(chk));
	fu_rts54hid_cmd_buffer_set_bufferlen(st, fu_chunk_get_data_sz(chk));
	fu_byte_array_set_size(st->buf, FU_RTS54FU_HID_REPORT_LENGTH, 0x0);

	if (!fu_memcpy_safe(st->buf->data,
			    st->buf->len,
			    FU_RTS54HID_CMD_BUFFER_OFFSET_DATA, /* dst */
			    fu_chunk_get_data(chk),
			    fu_chunk_get_data_sz(chk),
			    0x0, /* src */
			    fu_chunk_get_data_sz(chk),
			    error))
		return FALSE;

	if (!fu_rts54hid_device_set_report(FU_RTS54HID_DEVICE(self), st->buf, error)) {
		g_prefix_error(error, "failed to write flash @%08zx: ", fu_chunk_get_address(chk));
		return FALSE;
	}

	/* success */
	return TRUE;
}

static void
fu_rts54hid_device_init(FuRts54hidDevice *self)
{
	fu_device_add_protocol(FU_DEVICE(self), "com.realtek.rts54");
	fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_SIGNED_PAYLOAD);
	fu_device_set_version_format(FU_DEVICE(self), FWUPD_VERSION_FORMAT_PAIR);
}

static void
fu_rts54hid_device_class_init(FuRts54hidDeviceClass *klass)
{
	FuDeviceClass *device_class = FU_DEVICE_CLASS(klass);
	fu_device_register_private_flag(device_class, FU_RTS54HID_DEVICE_FLAG_FW_AUTH);
	fu_device_register_private_flag(device_class, FU_RTS54HID_DEVICE_FLAG_DUAL_BANK);
}
