/*
 * Copyright (C) 2021-2023 Synaptics Incorporated <simon.ho@synaptics.com>
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
		     FuFirmware)

FuFirmware *
fu_synaptics_cape_firmware_new(void);

guint16
fu_synaptics_cape_firmware_get_vid(FuSynapticsCapeFirmware *self);

guint16
fu_synaptics_cape_firmware_get_pid(FuSynapticsCapeFirmware *self);

#define FW_CAPE_FW_FILE_MAGIC_ID_HID  0x43534645
#define FW_CAPE_FW_FILE_MAGIC_ID_SNGL 0x4C474E53


#define FW_CAPE_HID_HEADER_OFFSET_VID	      0x0
#define FW_CAPE_HID_HEADER_OFFSET_PID	      0x4
#define FW_CAPE_HID_HEADER_OFFSET_UPDATE_TYPE 0x8
#define FW_CAPE_HID_HEADER_OFFSET_SIGNATURE   0xc
#define FW_CAPE_HID_HEADER_OFFSET_CRC	      0x10
#define FW_CAPE_HID_HEADER_OFFSET_VER_W	      0x14
#define FW_CAPE_HID_HEADER_OFFSET_VER_X	      0x16
#define FW_CAPE_HID_HEADER_OFFSET_VER_Y	      0x18
#define FW_CAPE_HID_HEADER_OFFSET_VER_Z	      0x1A



#define FW_CAPE_HID_HEADER_SIZE 32 /* =sizeof(FuCapeHidFileHeader) */


#define FW_CAPE_SNGL_HEADER_OFFSET_MAGIC      0x00
#define FW_CAPE_SNGL_HEADER_OFFSET_FILE_CRC   0x04
#define FW_CAPE_SNGL_HEADER_OFFSET_FILE_SIZE  0x08
#define FW_CAPE_SNGL_HEADER_OFFSET_MAGIC2     0x0C
#define FW_CAPE_SNGL_HEADER_OFFSET_IMG_TYPE   0x010
#define FW_CAPE_SNGL_HEADER_OFFSET_FW_VERSION 0x14
#define FW_CAPE_SNGL_HEADER_OFFSET_VID        0x18
#define FW_CAPE_SNGL_HEADER_OFFSET_PID        0x1A
#define FW_CAPE_SNGL_HEADER_OFFSET_FW_FILE_NUM 0x1C
#define FW_CAPE_SNGL_HEADER_OFFSET_VERSION    0x20
#define FW_CAPE_SNGL_HEADER_OFFSET_FW_CRC     0x24
#define FW_CAPE_SNGL_HEADER_OFFSET_MACHINE_NAME 0x30
#define FW_CAPE_SNGL_HEADER_OFFSET_TIME_STAMP 0x40
#define FW_CAPE_SNGL_HEADER_OFFSET_FW_FILE_LIST 0x50
#define FW_CAPE_FW_FILE_LIST_OFFSET_ID        0x0
#define FW_CAPE_FW_FILE_LIST_OFFSET_CRC       0x4
#define FW_CAPE_FW_FILE_LIST_OFFSET_FILE      0x8
#define FW_CAPE_FW_FILE_LIST_OFFSET_SIZE      0xC

#define FW_CAPE_SNGL_VERSION                  0X01000000
#define FW_CAPE_SNGL_IMG_TYPE_HID             0x00000001
#define FW_CAPE_SNGL_IMG_TYPE_SFS             0x00000002
#define FW_CAPE_SNGL_IMG_TYPE_SFS_OFFER       0x00000102
#define FW_CAPE_SNGL_IMG_TYPE_SFS_SIGN        0x00010002
#define FW_CAPE_SNGL_IMG_TYPE_SFS_OFFER_SIGN  0x00010102

#define FW_CAPE_SNGL_IMG_TYPE_ID_HID0 0x30444948 /*hid file for partition 0 */
#define FW_CAPE_SNGL_IMG_TYPE_ID_HID1 0x31444948 /*hid file for partition 1 */
#define FW_CAPE_SNGL_IMG_TYPE_ID_HOF0 0x30464F48 /*hid + offer file for partition 0 */
#define FW_CAPE_SNGL_IMG_TYPE_ID_HOF1 0x31464F48 /*hid + offer file for partition 1 */
#define FW_CAPE_SNGL_IMG_TYPE_ID_SFSX 0x58534653 /*sfs file*/
#define FW_CAPE_SNGL_IMG_TYPE_ID_SOFX 0x58464F53 /*sfs + offer file*/
#define FW_CAPE_SNGL_IMG_TYPE_ID_SIGN 0x4e474953 /*digital signature file*/

#define FW_CAPE_SNGL_MINIMUM_SIZE 0x18000

#define FW_CAPE_FIRMWARE_ID_HID_0 "hid-0"
#define FW_CAPE_FIRMWARE_ID_HID_1 "hid-1"
#define FW_CAPE_FIRMWARE_ID_HID_OFFER_0 "hid-offer-0"
#define FW_CAPE_FIRMWARE_ID_HID_OFFER_1 "hid-offer-1"
#define FW_CAPE_FIRMWARE_ID_HID_SIGNATURE_0 "hid-signature-0"
#define FW_CAPE_FIRMWARE_ID_HID_SIGNATURE_1 "hid-signature-1"
#define FW_CAPE_FIRMWARE_ID_SFS 			FU_FIRMWARE_ID_PAYLOAD
#define FW_CAPE_FIRMWARE_ID_SFS_OFFER   	"sfs-offer"
#define FW_CAPE_FIRMWARE_ID_SFS_SIGNATURE   FU_FIRMWARE_ID_SIGNATURE

#define FW_CAPE_FIRMWARE_EFS_TYPE_SYSTEM 1
#define FW_CAPE_FIRMWARE_EFS_TYPE_SFS    0x10

#define FW_CAPE_FIRMWARE_PARTITION_AUTO 0
#define FW_CAPE_FIRMWARE_PARTITION_1 1   //First partition
#define FW_CAPE_FIRMWARE_PARTITION_2 2
//FU_FIRMWARE_ID_SIGNATURE

