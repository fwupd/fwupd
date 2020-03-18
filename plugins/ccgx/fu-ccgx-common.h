/*
 * Copyright (C) 2020 Cypress Semiconductor Corporation.
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#pragma once

#include "config.h"

#include <string.h>
#include <sys/time.h>
#include <unistd.h>
#include "fu-device.h"
#include "fu-common-version.h"
#include "fu-usb-device.h"
#include "fwupd-error.h"

/* ccgx plugin version (internal only) major.minor.rev.build*/
#define CCGX_PLUGIN_VERSION	"0.1.0.4"

/* pd firmware application version */
typedef struct __attribute__((packed)) {
	union {
		struct {
			guint32 type  : 16;	/* application Type [LSB] */
			guint32 build : 8;	/* build Number */
			guint32 minor : 4;	/* minor Version */
			guint32 major : 4;	/* major Version [MSB] */
		}ver;
		guint32 val;			/* 32 bit version data */
	};
} PDFWAppVersion;

/* firmware mode in device */
typedef enum {
	FW_MODE_BOOT = 0,	/* boot mode */
	FW_MODE_FW1,		/* firmware 1 mode */
	FW_MODE_FW2,		/* firmware 2 mode */
	FW_MODE_MAX
} FWMode;

/* meta data Size */
#define CCGX_METADATA_SIZE	32

/* metadata valid signature "CY" */
#define CCGX_METADATA_VALID_SIG 0x4359

/* meta data */
typedef struct __attribute__((packed)) {
	guint8	fw_checksum;		/* firmware checksum */
	guint32	fw_entry;		/* firmware entry address */
	guint16	last_boot_row;		/* last flash row of bootloader or previous firmware */
	guint8	reserved1[2];		/* reserved */
	guint32	fw_size;		/* firmware size */
	guint8	reserved2[9];		/* reserved */
	guint16	metadata_valid;		/* meta data valid "CY" */
	guint8	reserved3[4];		/* reserved */
	guint32	boot_seq;		/* boot sequence number */
} CCGxMetaData;

/* device type for device manager */
typedef enum {
	DM_DEVICE_NONE=0,
	DM_DEVICE_EXTERNAL_BB,		/* external billboard for gen2 / hybrid Dock */
	DM_DEVICE_INTERNAL_BB,		/* internal billboard for cs18 Dock */
	DM_DEVICE_PD_I2C,		/* pd i2c device for gen2 / hybrid dock */
	DM_DEVICE_PD_I2CM,		/* pd i2cm device  for ds18 dock u2 */
	DM_DEVICE_PD_HID,		/* pd hid device for ds18 dock u1*/
	DM_DEVICE_DMC			/* dock management controller */
} DMDevice;

/* firmware Image type */
typedef enum {
	FW_IMAGE_TYPE_NONE=0,
	FW_IMAGE_TYPE_SINGLE,		/* single image */
	FW_IMAGE_TYPE_DUAL_SYMMETRIC,	/* dual symmetric image */
	FW_IMAGE_TYPE_DUAL_ASYMMETRIC	/* dual asymmetric image */
} FWImageType;

/* ccgx part information */
typedef struct __attribute__((packed)) {
	gint8	family_name[16];	/* family name of device */
	gint8	part_name[32];		/* part name of device */
	guint32	silicon_id;		/* 32 bit silicon ID */
	guint32	flash_row_size;		/* size of a flash row in Byte*/
	guint32	flash_size;		/* total size of Flash in Byte */
} CCGxPartInfo;

/* device data for pd device */
typedef struct __attribute__((packed)) {
	PDFWAppVersion	current_version;		/* application Version */
	PDFWAppVersion	fw_version[FW_MODE_MAX];	/* fw1 and fw2 verions */

	gboolean	fw_meta_valid;			/* fw1 and fw2 metadata valid */
	CCGxMetaData	fw_metadata[FW_MODE_MAX];	/* fw1 and fw2 metadata */

	guint16	fw1_meta_row_num;	/* fw1 meta row num */
	guint16	fw2_meta_row_num;	/* fw2 meta row num */
	guint32	fw_meta_offset;		/* offset of meta data in a row */
	guint32	fw_row_size;		/* row Size */

	FWMode	fw_mode;		/* firmware Mode */
	guint8	num_of_ports;		/* number of pd ports */
	guint16	silicon_id;		/* silicon id */
} PDDeviceData;

CCGxPartInfo	*fu_ccgx_util_find_ccgx_info		(guint16	 silicon_id);
