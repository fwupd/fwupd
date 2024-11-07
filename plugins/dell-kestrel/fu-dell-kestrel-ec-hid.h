/*
 * Copyright 2024 Dell Technologies
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later OR MIT
 */

#pragma once

#include <fwupdplugin.h>

#include "fu-dell-kestrel-ec-hid-struct.h"
#include "fu-dell-kestrel-ec-struct.h"

/* Used for EC HID communication */
#define FU_DELL_KESTREL_EC_HID_TIMEOUT	       2000
#define FU_DELL_KESTREL_EC_HID_CMD_FWUPDATE    0xAB
#define FU_DELL_KESTREL_EC_HID_EXT_FWUPDATE    0x80
#define FU_DELL_KESTREL_EC_HID_SUBCMD_FWUPDATE 0x00
#define FU_DELL_KESTREL_EC_DEV_EC_CHUNK_SZ     160000
#define FU_DELL_KESTREL_EC_DEV_ANY_CHUNK_SZ    180000
#define FU_DELL_KESTREL_EC_DEV_NO_CHUNK_SZ     G_MAXSIZE
#define FU_DELL_KESTREL_EC_HID_DATA_PAGE_SZ    192
#define FU_DELL_KESTREL_EC_HID_RESPONSE_LENGTH 0x03
#define FU_DELL_KESTREL_EC_HID_I2C_ADDRESS     0xec
#define FU_DELL_KESTREL_EC_HID_MAX_RETRIES     8

#define FU_DELL_KESTREL_HIDI2C_MAX_READ	 192
#define FU_DELL_KESTREL_HIDI2C_MAX_WRITE 128

gboolean
fu_dell_kestrel_ec_hid_write(FuDevice *device, GByteArray *buf, GError **error);

GBytes *
fu_dell_kestrel_ec_hid_fwup_pkg_new(FuChunk *chk,
				    gsize fw_sz,
				    FuDellKestrelEcDevType dev_type,
				    guint8 dev_identifier);

gboolean
fu_dell_kestrel_ec_hid_i2c_write(FuDevice *self, GByteArray *cmd_buf, GError **error);
gboolean
fu_dell_kestrel_ec_hid_i2c_read(FuDevice *self,
				FuDellKestrelEcHidCmd cmd,
				GByteArray *res,
				guint delayms,
				GError **error);
gboolean
fu_dell_kestrel_ec_hid_raise_mcu_clock(FuDevice *self, gboolean enable, GError **error);

gboolean
fu_dell_kestrel_ec_hid_erase_bank(FuDevice *self, guint8 idx, GError **error);

gboolean
fu_dell_kestrel_ec_hid_tbt_wake(FuDevice *self, GError **error);

gboolean
fu_dell_kestrel_ec_hid_tbt_write(FuDevice *self,
				 guint32 start_addr,
				 const guint8 *input,
				 gsize write_size,
				 GError **error);

gboolean
fu_dell_kestrel_ec_hid_tbt_authenticate(FuDevice *self, GError **error);
