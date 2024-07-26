/*
 * Copyright 2024 Dell Technologies
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later OR MIT
 */

#pragma once

#include <fwupdplugin.h>

/* Used for EC HID communication */
#define FU_DELL_K2_EC_HID_TIMEOUT	  2000
#define FU_DELL_K2_EC_HID_CMD_FWUPDATE	  0xAB
#define FU_DELL_K2_EC_HID_EXT_FWUPDATE	  0x80
#define FU_DELL_K2_EC_HID_SUBCMD_FWUPDATE 0x00
#define FU_DELL_K2_EC_HID_DATA_PAGE_SZ	  192
#define FU_DELL_K2_EC_HID_RESPONSE_LENGTH 0x03
#define FU_DELL_K2_EC_HID_I2C_ADDRESS	  0xec
#define FU_DELL_K2_EC_HID_MAX_RETRIES	  8

#define HUB_CMD_READ_DATA	0xC0
#define HUB_CMD_WRITE_DATA	0x40
#define HUB_EXT_READ_STATUS	0x09
#define HUB_EXT_MCUMODIFYCLOCK	0x06
#define HUB_EXT_I2C_WRITE	0xC6
#define HUB_EXT_WRITEFLASH	0xC8
#define HUB_EXT_I2C_READ	0xD6
#define HUB_EXT_VERIFYUPDATE	0xD9
#define HUB_EXT_ERASEBANK	0xE8
#define HUB_EXT_WRITE_TBT_FLASH 0xFF

#define HIDI2C_MAX_REGISTER 4
#define HIDI2C_MAX_READ	    192
#define HIDI2C_MAX_WRITE    128

gboolean
fu_dell_k2_ec_hid_write(FuDevice *device, GBytes *buf, GError **error);

GBytes *
fu_dell_k2_ec_hid_fwup_pkg_new(GBytes *fw, guint8 dev_type, guint8 dev_identifier);

gboolean
fu_dell_k2_ec_hid_i2c_write(FuDevice *self, const guint8 *input, gsize write_size, GError **error);
gboolean
fu_dell_k2_ec_hid_i2c_read(FuDevice *self,
			   guint32 cmd,
			   GByteArray *res,
			   guint delayms,
			   GError **error);
gboolean
fu_dell_k2_ec_hid_raise_mcu_clock(FuDevice *self, gboolean enable, GError **error);

gboolean
fu_dell_k2_ec_hid_erase_bank(FuDevice *self, guint8 idx, GError **error);

gboolean
fu_dell_k2_ec_hid_tbt_wake(FuDevice *self, GError **error);

gboolean
fu_dell_k2_ec_hid_tbt_write(FuDevice *self,
			    guint32 start_addr,
			    const guint8 *input,
			    gsize write_size,
			    GError **error);

gboolean
fu_dell_k2_ec_hid_tbt_authenticate(FuDevice *self, GError **error);
