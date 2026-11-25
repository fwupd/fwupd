/*
 * Copyright 2018 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include "fu-realtek-rts54hid-common.h"
#include "fu-realtek-rts54hid-device.h"
#include "fu-realtek-rts54hid-struct.h"

G_DEFINE_TYPE(FuRealtekRts54hidDevice, fu_realtek_rts54hid_device, FU_TYPE_HID_DEVICE)

gboolean
fu_realtek_rts54hid_device_set_report(FuRealtekRts54hidDevice *self,
				      GByteArray *buf,
				      GError **error)
{
	return fu_hid_device_set_report(FU_HID_DEVICE(self),
					0x0,
					buf->data,
					buf->len,
					FU_REALTEK_RTS54HID_DEVICE_TIMEOUT * 2,
					FU_HID_DEVICE_FLAG_NONE,
					error);
}

gboolean
fu_realtek_rts54hid_device_get_report(FuRealtekRts54hidDevice *self,
				      GByteArray *buf,
				      GError **error)
{
	return fu_hid_device_get_report(FU_HID_DEVICE(self),
					0x0,
					buf->data,
					buf->len,
					FU_REALTEK_RTS54HID_DEVICE_TIMEOUT,
					FU_HID_DEVICE_FLAG_NONE,
					error);
}

gboolean
fu_realtek_rts54hid_device_write_chunk(FuRealtekRts54hidDevice *self, FuChunk *chk, GError **error)
{
	g_autoptr(FuRealtekRts54hidCmdBuffer) st = fu_realtek_rts54hid_cmd_buffer_new();

	fu_realtek_rts54hid_cmd_buffer_set_cmd(st, FU_REALTEK_RTS54HID_CMD_WRITE_DATA);
	fu_realtek_rts54hid_cmd_buffer_set_ext(st, FU_REALTEK_RTS54HID_EXT_WRITEFLASH);
	fu_realtek_rts54hid_cmd_buffer_set_dwregaddr(st, fu_chunk_get_address(chk));
	fu_realtek_rts54hid_cmd_buffer_set_bufferlen(st, fu_chunk_get_data_sz(chk));
	fu_byte_array_set_size(st->buf, FU_RTS54FU_HID_REPORT_LENGTH, 0x0);

	if (!fu_memcpy_safe(st->buf->data,
			    st->buf->len,
			    FU_REALTEK_RTS54HID_CMD_BUFFER_OFFSET_DATA, /* dst */
			    fu_chunk_get_data(chk),
			    fu_chunk_get_data_sz(chk),
			    0x0, /* src */
			    fu_chunk_get_data_sz(chk),
			    error))
		return FALSE;

	if (!fu_realtek_rts54hid_device_set_report(FU_REALTEK_RTS54HID_DEVICE(self),
						   st->buf,
						   error)) {
		g_prefix_error(error, "failed to write flash @%08zx: ", fu_chunk_get_address(chk));
		return FALSE;
	}

	/* success */
	return TRUE;
}

static void
fu_realtek_rts54hid_device_init(FuRealtekRts54hidDevice *self)
{
	fu_device_add_protocol(FU_DEVICE(self), "com.realtek.rts54");
	fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_SIGNED_PAYLOAD);
	fu_device_set_version_format(FU_DEVICE(self), FWUPD_VERSION_FORMAT_PAIR);
}

static void
fu_realtek_rts54hid_device_class_init(FuRealtekRts54hidDeviceClass *klass)
{
	FuDeviceClass *device_class = FU_DEVICE_CLASS(klass);
	fu_device_register_private_flag(device_class, FU_REALTEK_RTS54HID_DEVICE_FLAG_FW_AUTH);
}
