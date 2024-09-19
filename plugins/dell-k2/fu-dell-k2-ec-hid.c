/*
 * Copyright 2024 Dell Technologies
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later OR MIT
 */

#include "config.h"

#include <errno.h>
#include <string.h>

#include "fu-dell-k2-ec-hid.h"

typedef struct __attribute__((packed)) { /* nocheck:blocked */
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
	} __attribute__((packed)) dev_fw_info; /* nocheck:blocked */
	guint8 *fw_data;
} FuEcHidFwupPkg;

typedef struct __attribute__((packed)) { /* nocheck:blocked */
	guint8 cmd;
	guint8 ext;
	union {
		guint32 dwregaddr;
		struct {
			guint8 cmd_data0;
			guint8 cmd_data1;
			guint8 cmd_data2;
			guint8 cmd_data3;
		};
	};
	guint16 bufferlen;
	struct FuHIDI2CParameters {
		guint8 i2ctargetaddr;
		guint8 regaddrlen;
		guint8 i2cspeed;
	} __attribute__((packed)) parameters; /* nocheck:blocked */
	guint8 extended_cmdarea[53];
	guint8 data[192];
} FuEcHIDCmdBuffer;

gboolean
fu_dell_k2_ec_hid_write(FuDevice *device, GBytes *buf, GError **error)
{
	guint8 *data = (guint8 *)g_bytes_get_data(buf, NULL);
	gsize data_sz = g_bytes_get_size(buf);

	return fu_hid_device_set_report(FU_HID_DEVICE(device),
					0x0,
					data,
					data_sz,
					FU_DELL_K2_EC_HID_TIMEOUT,
					FU_HID_DEVICE_FLAG_RETRY_FAILURE,
					error);
}

GBytes *
fu_dell_k2_ec_hid_fwup_pkg_new(GBytes *fw, guint8 dev_type, guint8 dev_identifier)
{
	g_autoptr(GByteArray) fwbuf = g_byte_array_new();
	gsize fw_size = g_bytes_get_size(fw);

	/* header */
	fu_byte_array_append_uint8(fwbuf, FU_DELL_K2_EC_HID_CMD_FWUPDATE);
	fu_byte_array_append_uint8(fwbuf, FU_DELL_K2_EC_HID_EXT_FWUPDATE);
	fu_byte_array_append_uint32(fwbuf, 7 + fw_size, G_BIG_ENDIAN); // 7 = sizeof(command)

	/* command */
	fu_byte_array_append_uint8(fwbuf, FU_DELL_K2_EC_HID_SUBCMD_FWUPDATE);
	fu_byte_array_append_uint8(fwbuf, dev_type);
	fu_byte_array_append_uint8(fwbuf, dev_identifier);
	fu_byte_array_append_uint32(fwbuf, fw_size, G_BIG_ENDIAN);

	/* data */
	fu_byte_array_append_bytes(fwbuf, fw);

	return g_bytes_new(fwbuf->data, fwbuf->len);
}

static gboolean
fu_dell_k2_ec_hid_set_report_cb(FuDevice *self, gpointer user_data, GError **error)
{
	guint8 *outbuffer = (guint8 *)user_data;
	return fu_hid_device_set_report(FU_HID_DEVICE(self),
					0x0,
					outbuffer,
					192,
					FU_DELL_K2_EC_HID_TIMEOUT * 3,
					FU_HID_DEVICE_FLAG_NONE,
					error);
}

static gboolean
fu_dell_k2_ec_hid_set_report(FuDevice *self, guint8 *outbuffer, GError **error)
{
	return fu_device_retry(self,
			       fu_dell_k2_ec_hid_set_report_cb,
			       FU_DELL_K2_EC_HID_MAX_RETRIES,
			       outbuffer,
			       error);
}

static gboolean
fu_dell_k2_ec_hid_get_report_cb(FuDevice *self, gpointer user_data, GError **error)
{
	guint8 *inbuffer = (guint8 *)user_data;
	return fu_hid_device_get_report(FU_HID_DEVICE(self),
					0x0,
					inbuffer,
					192,
					FU_DELL_K2_EC_HID_TIMEOUT,
					FU_HID_DEVICE_FLAG_NONE,
					error);
}

static gboolean
fu_dell_k2_ec_hid_get_report(FuDevice *self, guint8 *inbuffer, GError **error)
{
	return fu_device_retry(self,
			       fu_dell_k2_ec_hid_get_report_cb,
			       FU_DELL_K2_EC_HID_MAX_RETRIES,
			       inbuffer,
			       error);
}

gboolean
fu_dell_k2_ec_hid_raise_mcu_clock(FuDevice *self, gboolean enable, GError **error)
{
	FuEcHIDCmdBuffer cmd_buffer = {
	    .cmd = HUB_CMD_WRITE_DATA,
	    .ext = HUB_EXT_MCUMODIFYCLOCK,
	    .cmd_data0 = (guint8)enable,
	    .cmd_data1 = 0,
	    .cmd_data2 = 0,
	    .cmd_data3 = 0,
	    .bufferlen = 0,
	    .parameters = {0},
	    .extended_cmdarea[0 ... 52] = 0,
	};

	if (!fu_dell_k2_ec_hid_set_report(self, (guint8 *)&cmd_buffer, error)) {
		g_prefix_error(error, "failed to set mcu clock to %d: ", enable);
		return FALSE;
	}

	return TRUE;
}

gboolean
fu_dell_k2_ec_hid_erase_bank(FuDevice *self, guint8 idx, GError **error)
{
	FuEcHIDCmdBuffer cmd_buffer = {
	    .cmd = HUB_CMD_WRITE_DATA,
	    .ext = HUB_EXT_ERASEBANK,
	    .cmd_data0 = 0,
	    .cmd_data1 = idx,
	    .cmd_data2 = 0,
	    .cmd_data3 = 0,
	    .bufferlen = 0,
	    .parameters = {0},
	    .extended_cmdarea[0 ... 52] = 0,
	};

	if (!fu_dell_k2_ec_hid_set_report(self, (guint8 *)&cmd_buffer, error)) {
		g_prefix_error(error, "failed to erase bank: ");
		return FALSE;
	}

	return TRUE;
}

gboolean
fu_dell_k2_ec_hid_i2c_write(FuDevice *self, const guint8 *input, gsize write_size, GError **error)
{
	FuEcHIDCmdBuffer cmd_buffer = {
	    .cmd = HUB_CMD_WRITE_DATA,
	    .ext = HUB_EXT_I2C_WRITE,
	    .dwregaddr = 0,
	    .bufferlen = GUINT16_TO_LE(write_size),
	    .parameters = {0xEC, 0x1, 0x80},
	    .extended_cmdarea[0 ... 52] = 0,
	};

	g_return_val_if_fail(write_size <= HIDI2C_MAX_WRITE, FALSE);

	if (!fu_memcpy_safe(cmd_buffer.data,
			    sizeof(cmd_buffer.data),
			    0,
			    input,
			    write_size,
			    0,
			    write_size,
			    error))
		return FALSE;
	return fu_dell_k2_ec_hid_set_report(self, (guint8 *)&cmd_buffer, error);
}

gboolean
fu_dell_k2_ec_hid_i2c_read(FuDevice *self,
			   guint32 cmd,
			   GByteArray *res,
			   guint delayms,
			   GError **error)
{
	FuEcHIDCmdBuffer cmd_buffer = {
	    .cmd = HUB_CMD_WRITE_DATA,
	    .ext = HUB_EXT_I2C_READ,
	    .dwregaddr = GUINT32_TO_LE(cmd),
	    .bufferlen = GUINT16_TO_LE(res->len + 1),
	    .parameters = {0xEC, 0x1, 0x80},
	    .extended_cmdarea[0 ... 52] = 0,
	    .data[0 ... 191] = 0,
	};

	if (!fu_dell_k2_ec_hid_set_report(self, (guint8 *)&cmd_buffer, error))
		return FALSE;
	if (delayms > 0)
		fu_device_sleep(self, delayms);
	if (!fu_dell_k2_ec_hid_get_report(self, cmd_buffer.data, error))
		return FALSE;

	return fu_memcpy_safe(res->data,
			      res->len,
			      0,
			      cmd_buffer.data,
			      sizeof(cmd_buffer.data),
			      1,
			      res->len,
			      error);
}
