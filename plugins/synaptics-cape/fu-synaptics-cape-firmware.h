/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2021 Synaptics Incorporated <simon.ho@synaptics.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#pragma once

#include <fwupdplugin.h>

#define FU_TYPE_SYNAPTICS_CAPE_FIRMWARE (fu_synaptics_cape_firmware_get_type())

G_DECLARE_FINAL_TYPE(FuSynapticsCapeFirmware,
		     fu_synaptics_cape_firmware,
		     FU,
		     SYNAPTICS_CAPE_FIRMWARE,
		     FuSrecFirmware)

FuFirmware *
fu_synaptics_cape_firmware_new(void);

guint16
fu_synaptics_cape_firmware_get_vid(FuSynapticsCapeFirmware *self);

guint16
fu_synaptics_cape_firmware_get_pid(FuSynapticsCapeFirmware *self);

#define FW_CAPE_HID_HEADER_OFFSET_VID	      0x0
#define FW_CAPE_HID_HEADER_OFFSET_PID	      0x4
#define FW_CAPE_HID_HEADER_OFFSET_UPDATE_TYPE 0x8
#define FW_CAPE_HID_HEADER_OFFSET_SIGNATURE   0xc
#define FW_CAPE_HID_HEADER_OFFSET_CRC	      0x10
#define FW_CAPE_HID_HEADER_OFFSET_VER_W	      0x14
#define FW_CAPE_HID_HEADER_OFFSET_VER_X	      0x16
#define FW_CAPE_HID_HEADER_OFFSET_VER_Y	      0x18
#define FW_CAPE_HID_HEADER_OFFSET_VER_Z	      0x1C

/*Firmware update command structure, little endian */
typedef struct __attribute__((packed)) {
	guint32 vid;		/*USB vendor id*/
	guint32 pid;		/*USB product id*/
	guint32 fw_update_type; /*firmware update type*/
	guint32 fw_signature;	/*firmware identifier*/
	guint32 crc_value;	/*used to detect accidental changes to fw data*/
} FuCapeHidFwCmdUpdateStartPar;

typedef struct __attribute__((packed)) {
	FuCapeHidFwCmdUpdateStartPar par;
	guint16 version_w; /*firmware version is four parts number "z.y.x.w", this is last part */
	guint16 version_x; /*firmware version, thrid part*/
	guint16 version_y; /*firmware version, second part*/
	guint16 version_z; /*firmware version, first part*/
	guint32 reserved3;
} FuCapeHidFileHeader;
