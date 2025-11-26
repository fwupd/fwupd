/*
 * Copyright 2024 Dell Technologies
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later OR MIT
 */

#pragma once

#include <fwupdplugin.h>

#define FU_TYPE_DELL_KESTREL_RTSHUB (fu_dell_kestrel_rtshub_get_type())
G_DECLARE_FINAL_TYPE(FuDellKestrelRtshub,
		     fu_dell_kestrel_rtshub,
		     FU,
		     DELL_KESTREL_RTSHUB,
		     FuHidDevice)

/* Device IDs: USB Hub */
#define DELL_KESTREL_USB_RTS0_G1_PID 0xB0A1
#define DELL_KESTREL_USB_RTS0_G2_PID 0xB0A2
#define DELL_KESTREL_USB_RTS5_G2_PID 0xB0A3

/* USB RTSHUB HID COMMAND */
#define RTSHUB_CMD_READ_DATA	  0xC0
#define RTSHUB_CMD_WRITE_DATA	  0x40
#define RTSHUB_EXT_READ_STATUS	  0x09
#define RTSHUB_EXT_MCUMODIFYCLOCK 0x06
#define RTSHUB_EXT_WRITEFLASH	  0xC8
#define RTSHUB_EXT_VERIFYUPDATE	  0xD9
#define RTSHUB_EXT_ERASEBANK	  0xE8
#define RTSHUB_EXT_RESET_TO_FLASH 0xE9

/* USB RTSHUB HID COMMON */
#define DELL_KESTREL_RTSHUB_TIMEOUT		2000
#define DELL_KESTREL_RTSHUB_BUFFER_SIZE		192
#define DELL_KESTREL_RTSHUB_TRANSFER_BLOCK_SIZE 128

FuDellKestrelRtshub *
fu_dell_kestrel_rtshub_new(FuUsbDevice *device, FuDellDockBaseType dock_type);
