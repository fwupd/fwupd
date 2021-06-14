/*
 * Copyright (C) 2018 Realtek Semiconductor Corporation
 * Copyright (C) 2018 Dell Inc.
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
 * SPDX-License-Identifier: LGPL-2.1+ OR MIT
 */

#include "config.h"

#include <errno.h>
#include <fwupdplugin.h>
#include <string.h>

#include "fu-dell-dock-hid.h"

#define HIDI2C_MAX_REGISTER		4
#define HID_MAX_RETRIES			5
#define TBT_MAX_RETRIES			2
#define HIDI2C_TRANSACTION_TIMEOUT	2000

#define HUB_CMD_READ_DATA		0xC0
#define HUB_CMD_WRITE_DATA		0x40
#define HUB_EXT_READ_STATUS		0x09
#define HUB_EXT_MCUMODIFYCLOCK		0x06
#define HUB_EXT_I2C_WRITE		0xC6
#define HUB_EXT_WRITEFLASH		0xC8
#define HUB_EXT_I2C_READ		0xD6
#define HUB_EXT_VERIFYUPDATE		0xD9
#define HUB_EXT_ERASEBANK		0xE8
#define HUB_EXT_WRITE_TBT_FLASH		0xFF

#define TBT_COMMAND_WAKEUP		0x00000000
#define TBT_COMMAND_AUTHENTICATE	0xFFFFFFFF
#define TBT_COMMAND_AUTHENTICATE_STATUS	0xFFFFFFFE

typedef struct __attribute__ ((packed)) {
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
	FuHIDI2CParameters parameters;
	guint8 extended_cmdarea[53];
	guint8 data[192];
} FuHIDCmdBuffer;

typedef struct __attribute__ ((packed)) {
	guint8 cmd;
	guint8 ext;
	guint8 i2ctargetaddr;
	guint8 i2cspeed;
	union {
		guint32 startaddress;
		guint32 tbt_command;
	};
	guint8 bufferlen;
	guint8 extended_cmdarea[55];
	guint8 data[192];
} FuTbtCmdBuffer;

static gboolean
fu_dell_dock_hid_set_report_cb (FuDevice *self, gpointer user_data, GError **error)
{
	guint8 *outbuffer = (guint8 *) user_data;
	return fu_hid_device_set_report (FU_HID_DEVICE (self), 0x0,
					 outbuffer, 192,
					 HIDI2C_TRANSACTION_TIMEOUT,
					 FU_HID_DEVICE_FLAG_NONE,
					 error);
}

static gboolean
fu_dell_dock_hid_set_report (FuDevice *self,
			     guint8 *outbuffer,
			     GError **error)
{
	return fu_device_retry (self, fu_dell_dock_hid_set_report_cb,
				HID_MAX_RETRIES, outbuffer, error);
}

static gboolean
fu_dell_dock_hid_get_report_cb (FuDevice *self, gpointer user_data, GError **error)
{
	guint8 *inbuffer = (guint8 *) user_data;
	return fu_hid_device_get_report (FU_HID_DEVICE (self), 0x0,
					 inbuffer, 192,
					 HIDI2C_TRANSACTION_TIMEOUT,
					 FU_HID_DEVICE_FLAG_NONE,
					 error);
}

static gboolean
fu_dell_dock_hid_get_report (FuDevice *self,
			     guint8 *inbuffer,
			     GError **error)
{
	return fu_device_retry (self, fu_dell_dock_hid_get_report_cb,
				HID_MAX_RETRIES, inbuffer, error);
}

gboolean
fu_dell_dock_hid_get_hub_version (FuDevice *self,
				  GError **error)
{
	g_autofree gchar *version = NULL;
	FuHIDCmdBuffer cmd_buffer = {
	    .cmd = HUB_CMD_READ_DATA,
	    .ext = HUB_EXT_READ_STATUS,
	    .cmd_data0 = 0,
	    .cmd_data1 = 0,
	    .cmd_data2 = 0,
	    .cmd_data3 = 0,
	    .bufferlen = GUINT16_TO_LE (12),
	    .parameters = {.i2ctargetaddr = 0, .regaddrlen = 0, .i2cspeed = 0},
	    .extended_cmdarea[0 ... 52] = 0,
	};

	if (!fu_dell_dock_hid_set_report (self, (guint8 *) &cmd_buffer,
					  error)) {
		g_prefix_error (error, "failed to query hub version: ");
		return FALSE;
	}
	if (!fu_dell_dock_hid_get_report (self, cmd_buffer.data, error)) {
		g_prefix_error (error, "failed to query hub version: ");
		return FALSE;
	}

	version = g_strdup_printf ("%02x.%02x",
				   cmd_buffer.data[10],
				   cmd_buffer.data[11]);
	fu_device_set_version_format (self, FWUPD_VERSION_FORMAT_PAIR);
	fu_device_set_version (self, version);
	return TRUE;
}

gboolean
fu_dell_dock_hid_raise_mcu_clock (FuDevice *self,
				  gboolean enable,
				  GError **error)
{
	FuHIDCmdBuffer cmd_buffer = {
	    .cmd = HUB_CMD_WRITE_DATA,
	    .ext = HUB_EXT_MCUMODIFYCLOCK,
	    .cmd_data0 = (guint8) enable,
	    .cmd_data1 = 0,
	    .cmd_data2 = 0,
	    .cmd_data3 = 0,
	    .bufferlen = 0,
	    .parameters = {.i2ctargetaddr = 0, .regaddrlen = 0, .i2cspeed = 0},
	    .extended_cmdarea[0 ... 52] = 0,
	};

	if (!fu_dell_dock_hid_set_report (self, (guint8 *) &cmd_buffer,
					  error)) {
		g_prefix_error (error,
				"failed to set mcu clock to %d: ",
				enable);
		return FALSE;
	}

	return TRUE;
}

gboolean
fu_dell_dock_hid_get_ec_status (FuDevice *self,
				guint8 *status1,
				guint8 *status2,
				GError **error)
{
	FuHIDCmdBuffer cmd_buffer = {
	    .cmd = HUB_CMD_WRITE_DATA,
	    .ext = HUB_EXT_READ_STATUS,
	    .cmd_data0 = 0,
	    .cmd_data1 = 0,
	    .cmd_data2 = 0,
	    .cmd_data3 = 0,
	    .bufferlen = GUINT16_TO_LE (27),
	    .parameters = {.i2ctargetaddr = 0, .regaddrlen = 0, .i2cspeed = 0},
	    .extended_cmdarea[0 ... 52] = 0,
	};

	if (!fu_dell_dock_hid_set_report (self, (guint8 *) &cmd_buffer,
					  error)) {
		g_prefix_error (error, "failed to get EC status: ");
		return FALSE;
	}
	if (!fu_dell_dock_hid_get_report (self, cmd_buffer.data, error)) {
		g_prefix_error (error, "failed to get EC status: ");
		return FALSE;
	}

	*status1 = cmd_buffer.data[25];
	*status2 = cmd_buffer.data[26];

	return TRUE;
}

gboolean
fu_dell_dock_hid_erase_bank (FuDevice *self, guint8 idx, GError **error)
{
	FuHIDCmdBuffer cmd_buffer = {
	    .cmd = HUB_CMD_WRITE_DATA,
	    .ext = HUB_EXT_ERASEBANK,
	    .cmd_data0 = 0,
	    .cmd_data1 = idx,
	    .cmd_data2 = 0,
	    .cmd_data3 = 0,
	    .bufferlen = 0,
	    .parameters = {.i2ctargetaddr = 0, .regaddrlen = 0, .i2cspeed = 0},
	    .extended_cmdarea[0 ... 52] = 0,
	};

	if (!fu_dell_dock_hid_set_report (self, (guint8 *) &cmd_buffer,
					  error)) {
		g_prefix_error (error, "failed to erase bank: ");
		return FALSE;
	}

	return TRUE;
}

gboolean
fu_dell_dock_hid_write_flash (FuDevice *self,
			      guint32 dwAddr,
			      const guint8 *input,
			      gsize write_size,
			      GError **error)
{
	FuHIDCmdBuffer cmd_buffer = {
	    .cmd = HUB_CMD_WRITE_DATA,
	    .ext = HUB_EXT_WRITEFLASH,
	    .dwregaddr = GUINT32_TO_LE (dwAddr),
	    .bufferlen = GUINT16_TO_LE (write_size),
	    .parameters = {.i2ctargetaddr = 0, .regaddrlen = 0, .i2cspeed = 0},
	    .extended_cmdarea[0 ... 52] = 0,
	};

	g_return_val_if_fail (write_size <= HIDI2C_MAX_WRITE, FALSE);

	memcpy (cmd_buffer.data, input, write_size);
	if (!fu_dell_dock_hid_set_report (self, (guint8 *) &cmd_buffer,
					  error)) {
		g_prefix_error (
		    error, "failed to write %" G_GSIZE_FORMAT " flash to %x: ",
		    write_size, dwAddr);
		return FALSE;
	}

	return TRUE;
}

gboolean
fu_dell_dock_hid_verify_update (FuDevice *self,
				gboolean *result,
				GError **error)
{
	FuHIDCmdBuffer cmd_buffer = {
	    .cmd = HUB_CMD_WRITE_DATA,
	    .ext = HUB_EXT_VERIFYUPDATE,
	    .cmd_data0 = 1,
	    .cmd_data1 = 0,
	    .cmd_data2 = 0,
	    .cmd_data3 = 0,
	    .bufferlen = GUINT16_TO_LE (1),
	    .parameters = {.i2ctargetaddr = 0, .regaddrlen = 0, .i2cspeed = 0},
	    .extended_cmdarea[0 ... 52] = 0,
	};

	if (!fu_dell_dock_hid_set_report (self, (guint8 *) &cmd_buffer,
				     error)) {
		g_prefix_error (error, "failed to verify update: ");
		return FALSE;
	}
	if (!fu_dell_dock_hid_get_report (self, cmd_buffer.data, error)) {
		g_prefix_error (error, "failed to verify update: ");
		return FALSE;
	}
	*result = cmd_buffer.data[0];

	return TRUE;
}

gboolean
fu_dell_dock_hid_i2c_write (FuDevice *self,
			    const guint8 *input,
			    gsize write_size,
			    const FuHIDI2CParameters *parameters,
			    GError **error)
{
	FuHIDCmdBuffer cmd_buffer = {
	    .cmd = HUB_CMD_WRITE_DATA,
	    .ext = HUB_EXT_I2C_WRITE,
	    .dwregaddr = 0,
	    .bufferlen = GUINT16_TO_LE (write_size),
	    .parameters = {.i2ctargetaddr = parameters->i2ctargetaddr,
			   .regaddrlen = 0,
			   .i2cspeed = parameters->i2cspeed | 0x80},
	    .extended_cmdarea[0 ... 52] = 0,
	};

	g_return_val_if_fail (write_size <= HIDI2C_MAX_WRITE, FALSE);

	memcpy (cmd_buffer.data, input, write_size);

	return fu_dell_dock_hid_set_report (self, (guint8 *) &cmd_buffer, error);
}

gboolean
fu_dell_dock_hid_i2c_read (FuDevice *self,
			   guint32 cmd,
			   gsize read_size,
			   GBytes **bytes,
			   const FuHIDI2CParameters *parameters,
			   GError **error)
{
	FuHIDCmdBuffer cmd_buffer = {
	    .cmd = HUB_CMD_WRITE_DATA,
	    .ext = HUB_EXT_I2C_READ,
	    .dwregaddr = GUINT32_TO_LE (cmd),
	    .bufferlen = GUINT16_TO_LE (read_size),
	    .parameters = {.i2ctargetaddr = parameters->i2ctargetaddr,
			   .regaddrlen = parameters->regaddrlen,
			   .i2cspeed = parameters->i2cspeed | 0x80},
	    .extended_cmdarea[0 ... 52] = 0,
	    .data[0 ... 191] = 0,
	};

	g_return_val_if_fail (read_size <= HIDI2C_MAX_READ, FALSE);
	g_return_val_if_fail (bytes != NULL, FALSE);
	g_return_val_if_fail (parameters->regaddrlen < HIDI2C_MAX_REGISTER, FALSE);

	if (!fu_dell_dock_hid_set_report (self, (guint8 *) &cmd_buffer, error))
		return FALSE;
	if (!fu_dell_dock_hid_get_report (self, cmd_buffer.data, error))
		return FALSE;

	*bytes = g_bytes_new (cmd_buffer.data, read_size);

	return TRUE;
}

gboolean
fu_dell_dock_hid_tbt_wake (FuDevice *self,
			   const FuHIDI2CParameters *parameters,
			   GError **error)
{
	FuTbtCmdBuffer cmd_buffer = {
	    .cmd = HUB_CMD_READ_DATA, /* special write command that reads status result */
	    .ext = HUB_EXT_WRITE_TBT_FLASH,
	    .i2ctargetaddr = parameters->i2ctargetaddr,
	    .i2cspeed = parameters->i2cspeed, /* unlike other commands doesn't need | 0x80 */
	    .tbt_command = TBT_COMMAND_WAKEUP,
	    .bufferlen = 0,
	    .extended_cmdarea[0 ... 53] = 0,
	    .data[0 ... 191] = 0,
	};

	if (!fu_dell_dock_hid_set_report (self, (guint8 *) &cmd_buffer, error)) {
		g_prefix_error (error, "failed to set wake thunderbolt: ");
		return FALSE;
	}
	if (!fu_dell_dock_hid_get_report (self, cmd_buffer.data, error)) {
		g_prefix_error (error, "failed to get wake thunderbolt status: ");
		return FALSE;
	}
	g_debug ("thunderbolt wake result: 0x%x", cmd_buffer.data[1]);

	return TRUE;
}

static const gchar *
fu_dell_dock_hid_tbt_map_error (guint32 code)
{
	if (code == 1)
		return g_strerror (EINVAL);
	else if (code == 2)
		return g_strerror (EPERM);

	return g_strerror (EIO);
}

gboolean
fu_dell_dock_hid_tbt_write (FuDevice *self,
			    guint32 start_addr,
			    const guint8 *input,
			    gsize write_size,
			    const FuHIDI2CParameters *parameters,
			    GError **error)
{
	FuTbtCmdBuffer cmd_buffer = {
	    .cmd = HUB_CMD_READ_DATA, /* It's a special write command that reads status result */
	    .ext = HUB_EXT_WRITE_TBT_FLASH,
	    .i2ctargetaddr = parameters->i2ctargetaddr,
	    .i2cspeed = parameters->i2cspeed, /* unlike other commands doesn't need | 0x80 */
	    .startaddress = GUINT32_TO_LE (start_addr),
	    .bufferlen = write_size,
	    .extended_cmdarea[0 ... 53] = 0,
	};
	guint8 result;

	g_return_val_if_fail (input != NULL, FALSE);
	g_return_val_if_fail (write_size <= HIDI2C_MAX_WRITE, FALSE);

	memcpy (cmd_buffer.data, input, write_size);

	for (gint i = 1; i <= TBT_MAX_RETRIES; i++) {
		if (!fu_dell_dock_hid_set_report (self, (guint8 *) &cmd_buffer, error)) {
			g_prefix_error (error, "failed to run TBT update: ");
			return FALSE;
		}
		if (!fu_dell_dock_hid_get_report (self, cmd_buffer.data, error)) {
			g_prefix_error (error, "failed to get TBT flash status: ");
			return FALSE;
		}
		result = cmd_buffer.data[1] & 0xf;
		if (result == 0)
			break;
		g_debug ("attempt %d/%d: Thunderbolt write failed: %x",
			  i, TBT_MAX_RETRIES, result);
	}
	if (result != 0) {
		g_set_error (error, FWUPD_ERROR, FWUPD_ERROR_INTERNAL,
			     "Writing address 0x%04x failed: %s",
			     start_addr, fu_dell_dock_hid_tbt_map_error (result));
		return FALSE;
	}

	return TRUE;
}

gboolean
fu_dell_dock_hid_tbt_authenticate (FuDevice *self,
				   const FuHIDI2CParameters *parameters,
				   GError **error)
{
	FuTbtCmdBuffer cmd_buffer = {
	    .cmd = HUB_CMD_READ_DATA, /* It's a special write command that reads status result */
	    .ext = HUB_EXT_WRITE_TBT_FLASH,
	    .i2ctargetaddr = parameters->i2ctargetaddr,
	    .i2cspeed = parameters->i2cspeed, /* unlike other commands doesn't need | 0x80 */
	    .tbt_command = GUINT32_TO_LE (TBT_COMMAND_AUTHENTICATE),
	    .bufferlen = 0,
	    .extended_cmdarea[0 ... 53] = 0,
	};
	guint8 result;

	if (!fu_dell_dock_hid_set_report (self, (guint8 *) &cmd_buffer, error)) {
		g_prefix_error (error, "failed to send authentication: ");
		return FALSE;
	}

	cmd_buffer.tbt_command = GUINT32_TO_LE (TBT_COMMAND_AUTHENTICATE_STATUS);
	/* needs at least 2 seconds */
	g_usleep (2000000);
	for (gint i = 1; i <= TBT_MAX_RETRIES; i++) {
		if (!fu_dell_dock_hid_set_report (self, (guint8 *) &cmd_buffer, error)) {
			g_prefix_error (error, "failed to set check authentication: ");
			return FALSE;
		}
		if (!fu_dell_dock_hid_get_report (self, cmd_buffer.data, error)) {
			g_prefix_error (error, "failed to get check authentication: ");
			return FALSE;
		}
		result = cmd_buffer.data[1] & 0xf;
		if (result == 0)
			break;
		g_debug ("attempt %d/%d: Thunderbolt authenticate failed: %x",
			  i, TBT_MAX_RETRIES, result);
		g_usleep (500000);
	}
	if (result != 0) {
		g_set_error (error, G_IO_ERROR, G_IO_ERROR_FAILED,
			     "Thunderbolt authentication failed: %s",
			     fu_dell_dock_hid_tbt_map_error (result));
		return FALSE;
	}

	return TRUE;
}
