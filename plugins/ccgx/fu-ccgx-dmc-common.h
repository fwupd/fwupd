/*
 * Copyright (C) 2020 Cypress Semiconductor Corporation.
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#pragma once

#include <glib.h>

/* maximum number of programmable devices expected to be connected in dock.
 * this is design limitation. This shall not be edited, unless stated by C Y*/
#define DMC_DOCK_MAX_DEV_COUNT 16

/* size of FW version structure in bytes */
#define DMC_DOCK_FW_VERSION_SIZE 8

/* indicates length of string in dock identity*/
#define DMC_IDENTITY_STRING_LEN 32

/* interrupt end point for DMC Dock */
#define DMC_INTERRUPT_PIPE_ID 0x82

/* USB bulk end point for DMC Dock */
#define DMC_BULK_PIPE_ID 1

/* indicates length of interrupt structure's data array filed */
#define DMC_INTERRUPT_DATA_LEN 8

/* status of the dmc will have different length. so first few bytes of status
 * is read, which will contain actual length of status. this value indicates
 * how much byte should read at first stage */
#define DMC_GET_STATUS_MIN_LEN 32

#define DMC_HASH_SIZE 32

/* time out to be set in control in/out pipe policy in ms */
#define DMC_CONTROL_TRANSFER_DEFAULT_TIMEOUT 5000

/* time out to be set in bulk out pipe policy in ms */
#define DMC_BULK_OUT_PIPE_TIMEOUT 2000

/* time out to be set in bulk out pipe policy in ms */
#define DMC_GET_REQUEST_TIMEOUT 20000

#define DMC_FWCT_SIGN 0x54435746 /* 'F' 'W' 'C' 'T' */

/* first we have to read few bytes to know the actual length of FWCT
 * this constant defines, number bytest to be read for getting length */
#define DMC_FWCT_MIN_LENGTH    6
#define DMC_FWCT_LENGTH_OFFSET 4
#define DMC_FWCT_MAX_SIZE      2048

#define DMC_CUSTOM_META_LENGTH_FIELD_SIZE 2
#define DMC_CUSTOM_META_LENGTH_OFFSET	  0
#define DMC_CUSTOM_META_MAX_SIZE	  256

/* this data type enumerates the image types */
typedef enum { DMC_IMG_TYPE_INVALID = 0, DMC_IMG_TYPE_IMAGE_0, DMC_IMG_TYPE_IMAGE_1 } DmcImgType;

/* this data type enumerates the image status */
typedef enum {
	DMC_IMG_STATUS_UNKNOWN = 0,
	DMC_IMG_STATUS_VALID,
	DMC_IMG_STATUS_INVALID,
	DMC_IMG_STATUS_RECOVERY,
	DMC_IMG_STATUS_RECOVERED_FROM_SECONDARY,
	DMC_IMG_STATUS_NOT_SUPPORTED = 0x0F
} DmcImgStatus;

/* this data type enumerates the image modes or flash architecture */
typedef enum {
	/* indicates that the device has a single image */
	DMC_IMG_MODE_SINGLE_IMG = 0,
	/* the device supports symmetric boot. In symmetric mode the bootloader
	 * boots the image with higher version, when they are valid */
	DMC_IMG_MODE_DUAL_IMG_SYM,
	/* the device supports Asymmetric boot. Image-1 & 2 can be different or
	 * same. in this method Bootloader is hard coded to boot the primary
	 * image. Secondary acts as recovery */
	DMC_IMG_MODE_DUAL_IMG_ASYM,
	DMC_IMG_MODE_SINGLE_IMG_WITH_RAM_IMG,
} DmcImgMode;

/* this data type enumerates the dock status */
typedef enum {
	/* status code indicating DOCK IDLE state. SUCCESS: no malfunctioning
	 * no outstanding request or event */
	DMC_DEVICE_STATUS_IDLE = 0,
	/* status code indicating dock FW update in progress */
	DMC_DEVICE_STATUS_UPDATE_IN_PROGRESS,
	/* status code indicating dock FW update is partially complete */
	DMC_DEVICE_STATUS_UPDATE_PARTIAL,
	/* status code indicating dock FW update SUCCESS - all m_images of all
	 * devices are valid */
	DMC_DEVICE_STATUS_UPDATE_COMPLETE_FULL,
	/* status code indicating dock FW update SUCCESS - not all m_images of all
	 * devices are valid */
	DMC_DEVICE_STATUS_UPDATE_COMPLETE_PARTIAL,
	/* fw download status */
	DMC_DEVICE_STATUS_UPDATE_PHASE_1_COMPLETE,
	DMC_DEVICE_STATUS_FW_DOWNLOADED_UPDATE_PEND,
	DMC_DEVICE_STATUS_FW_DOWNLOADED_PARTIAL_UPDATE_PEND,
	DMC_DEVICE_STATUS_PHASE2_UPDATE_IN_PROGRESS = 0x81,
	DMC_DEVICE_STATUS_PHASE2_UPDATE_PARTIAL,
	DMC_DEVICE_STATUS_PHASE2_UPDATE_FACTORY_BACKUP,
	DMC_DEVICE_STATUS_PHASE2_UPDATE_COMPLETE_PARTIAL,
	DMC_DEVICE_STATUS_PHASE2_UPDATE_COMPLETE_FULL,
	DMC_DEVICE_STATUS_PHASE2_UPDATE_FAIL_INVALID_FWCT,
	DMC_DEVICE_STATUS_PHASE2_UPDATE_FAIL_INVALID_DOCK_IDENTITY,
	DMC_DEVICE_STATUS_PHASE2_UPDATE_FAIL_INVALID_COMPOSITE_VER,
	DMC_DEVICE_STATUS_PHASE2_UPDATE_FAIL_AUTHENTICATION_FAILED,
	DMC_DEVICE_STATUS_PHASE2_UPDATE_FAIL_INVALID_ALGORITHM,
	DMC_DEVICE_STATUS_PHASE2_UPDATE_FAIL_SPI_READ_FAILED,
	DMC_DEVICE_STATUS_PHASE2_UPDATE_FAIL_NO_VALID_KEY,
	DMC_DEVICE_STATUS_PHASE2_UPDATE_FAIL_NO_VALID_SPI_PACKAGE,
	DMC_DEVICE_STATUS_PHASE2_UPDATE_FAIL_RAM_INIT_FAILED,
	DMC_DEVICE_STATUS_PHASE2_UPDATE_FAIL_FACTORY_BACKUP_FAILED,
	DMC_DEVICE_STATUS_PHASE2_UPDATE_FAIL_NO_VALID_FACTORY_PACKAGE,
	/* status code indicating dock FW update FAILED */
	DMC_DEVICE_STATUS_UPDATE_FAIL = 0xFF
} DmcDeviceStatus;

/* this data type enumerates the request codes for vendor interface */
typedef enum {
	DMC_RQT_CODE_UPGRADE_START = 0xD0,
	DMC_RQT_CODE_RESERV_0,
	DMC_RQT_CODE_FWCT_WRITE,
	DMC_RQT_CODE_IMG_WRITE,
	DMC_RQT_CODE_RESERV_1,
	DMC_RQT_CODE_RESERV_2,
	DMC_RQT_CODE_DOCK_STATUS,
	DMC_RQT_CODE_DOCK_IDENTITY,
	/* command to reset dmc state machine of DMC */
	DMC_RQT_CODE_RESET_STATE_MACHINE,
	/* command to reset for online enhanced mode (no reset during update) */
	DMC_RQT_CODE_SOFT_RESET = 0xDC,
	/* Update Trigger command for offline mode */
	DMC_RQT_CODE_TRIGGER = 0xDA
} DmcRqtCode;

/* this data type enumerates the opcode of interrupt read */
typedef enum {
	DMC_INT_OPCODE_FW_UPGRADE_RQT = 1,
	DMC_INT_OPCODE_FW_UPGRADE_STATUS = 0x80,
	DMC_INT_OPCODE_IMG_WRITE_STATUS,
	DMC_INT_OPCODE_REENUM,
	DMC_INT_OPCODE_FWCT_ANALYSIS_STATUS
} DmcIntOpcode;

/* this data type enumerates the fwct analysis status */
typedef enum {
	DMC_FWCT_ANALYSIS_STATUS_INVALID_FWCT = 0,
	DMC_FWCT_ANALYSIS_STATUS_INVALID_DOCK_IDENTITY,
	DMC_FWCT_ANALYSIS_STATUS_INVALID_COMPOSITE_VERSION,
	DMC_FWCT_ANALYSIS_STATUS_AUTHENTICATION_FAILED,
	DMC_FWCT_ANALYSIS_STATUS_INVALID_ALGORITHM
} DmcFwctAnalysisStatus;

typedef enum {
	DMC_UPDATE_MODEL_NONE = 0,
	/* need to trigger after updating FW */
	DMC_UPDATE_MODEL_DOWNLOAD_TRIGGER,
	/* need to set soft reset after updating FW */
	DMC_UPDATE_MODEL_PENDING_RESET,
} DmcUpdateModel;

/* this structure defines the fields of data returned when reading dock_identity
 * for new firmware */
typedef struct __attribute__((packed)) {
	/* this field indicates both validity and structure version
	 * 0 : invalid
	 * 1 : old structure
	 * 2 : new structure */
	guint8 structure_version;
	guint8 cdtt_version;
	guint16 vid;
	guint16 pid;
	guint16 device_id;
	gchar vendor_string[DMC_IDENTITY_STRING_LEN];
	gchar product_string[DMC_IDENTITY_STRING_LEN];
	guint8 custom_meta_data_flag;
	/* model field indicates the type of the firmware upgrade status
	 * 0 - online/offline
	 * 1 - Online model
	 * 2 - ADICORA/Offline model
	 * 3 - No reset
	 * 4 - 0xFF - Reserved
	 */
	guint8 model;
} DmcDockIdentity;

/* this structure defines the fields of status of a specific device  */
typedef struct __attribute__((packed)) {
	/* device ID of the device */
	guint8 device_type;
	/* component ID of the device */
	guint8 component_id;
	/* image mode of the device - single image/ dual symmetric/ dual
	 * asymmetric image > */
	guint8 image_mode;
	/* current running image */
	guint8 current_image;
	/* image status
	 * b7:b4 => Image 2 status
	 * b3:b0 => Image 1 status
	 *  0 = Unknown
	 *  1 = Valid
	 *  2 = Invalid
	 *  3-0xF = Reserved
	 */
	guint8 img_status;
	/* padding */
	guint8 reserved_0[3];
	/* complete fw version 8 bytes for bootload, image1 and image2. 8 byte
	 * for fw version and application version */
	guint8 fw_version[24];
} DmcDevxStatus;

/* this structure defines the fields of data returned when reading dock_status */
typedef struct __attribute__((packed)) {
	/* overall status of dock. see DmcDeviceStatus */
	guint8 device_status;
	/* eevice count */
	guint8 device_count;
	/* length of status bytes including dock_status,
	devx_status for each device */
	guint16 status_length;
	/* dock composite version m_fwct_info */
	guint32 composite_version;
	/* fw status of device of interest */
	DmcDevxStatus devx_status[DMC_DOCK_MAX_DEV_COUNT];
} DmcDockStatus;

/* This structure defines the fields of data returned when reading an interrupt
 * from DMC */
typedef struct __attribute__((packed)) {
	guint8 opcode;
	guint8 length;
	guint8 data[DMC_INTERRUPT_DATA_LEN];
} DmcIntRqt;

/* this structure defines header structure of FWCT */
typedef struct __attribute__((packed)) {
	guint32 signature;
	guint16 size;
	guint8 checksum;
	guint8 version;
	guint8 custom_meta_type;
	guint8 cdtt_version;
	guint16 vid;
	guint16 pid;
	guint16 device_id;
	guint8 reserv0[16];
	guint32 composite_version;
	guint8 image_count;
	guint8 reserv1[3];
} FwctInfo;

typedef struct __attribute__((packed)) {
	guint8 device_type;
	guint8 img_type;
	guint8 comp_id;
	guint8 row_size;
	guint8 reserv0[4];
	guint32 fw_version;
	guint32 app_version;
	guint32 img_offset;
	guint32 img_size;
	guint8 img_digest[32];
	guint8 num_img_segments;
	guint8 reserv1[3];
} FwctImageInfo;

typedef struct __attribute__((packed)) {
	guint8 img_id;
	guint8 type;
	guint16 start_row;
	guint16 num_rows; /* size */
	guint8 reserv0[2];
} FwctSegmentationInfo;

const gchar *
fu_ccgx_dmc_update_model_type_to_string(DmcUpdateModel val);
