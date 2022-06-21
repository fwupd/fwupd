/*
 * Copyright (C) 2022 Intel
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#pragma once

#include <glib.h>

#define HECI1_CSE_FS_FWUPDATE_STATE_IDLE_BIT (1 << 11)
#define HECI1_CSE_FS_INITSTATE_COMPLETED_BIT (1 << 9)
#define HECI1_CSE_GS1_PHASE_FWUPDATE	     7
#define HECI1_CSE_FS_FWUPD_PHASE_SHIFT	     28
#define HECI1_CSE_FS_FWUPD_PHASE_MASK	     0xF
#define HECI1_CSE_FS_FWUPD_PERCENT_SHIFT     16
#define HECI1_CSE_FS_FWUPD_PERCENT_MASK	     0xFF
#define HECI1_CSE_FS_MODE_MASK		     0x3
#define HECI1_CSE_FS_CP_MODE		     0x3

enum gsc_fwu_heci_partition_version {
	GSC_FWU_HECI_PART_VERSION_INVALID = 0,
	GSC_FWU_HECI_PART_VERSION_GFX_FW = 1,
	GSC_FWU_HECI_PART_VERSION_OPROM_DATA = 2,
	GSC_FWU_HECI_PART_VERSION_OPROM_CODE = 3,
};

enum gsc_fwu_heci_payload_type {
	GSC_FWU_HECI_PAYLOAD_TYPE_INVALID = 0,
	GSC_FWU_HECI_PAYLOAD_TYPE_GFX_FW = 1,
	GSC_FWU_HECI_PAYLOAD_TYPE_OPROM_DATA = 2,
	GSC_FWU_HECI_PAYLOAD_TYPE_OPROM_CODE = 3,
	GSC_FWU_HECI_PAYLOAD_TYPE_FWDATA = 5,
};

enum gsc_fwu_heci_command_id {
	GSC_FWU_HECI_COMMAND_ID_INVALID = 0,
	GSC_FWU_HECI_COMMAND_ID_START,			  /* start firmware updated flow      */
	GSC_FWU_HECI_COMMAND_ID_DATA,			  /* send firmware data to device     */
	GSC_FWU_HECI_COMMAND_ID_END,			  /* last command in update           */
	GSC_FWU_HECI_COMMAND_ID_GET_VERSION,		  /* retrieve version of a firmware   */
	GSC_FWU_HECI_COMMAND_ID_NO_UPDATE,		  /* do not wait for firmware update  */
	GSC_FWU_HECI_COMMAND_ID_GET_IP_VERSION,		  /* retrieve version of a partition  */
	GSC_FWU_HECI_COMMAND_ID_GET_CONFIG,		  /* get hardware config               */
	GSC_FWU_HECI_COMMAND_ID_STATUS,			  /* get status of most recent update */
	GSC_FWU_HECI_COMMAND_ID_GET_GFX_DATA_UPDATE_INFO, /* get signed firmware data info    */
	GSC_FWU_HECI_COMMAND_ID_GET_SUBSYSTEM_IDS,	  /* get subsystem ids (vid/did)      */
	GSC_FWU_HECI_COMMAND_MAX
};

struct gsc_fwu_heci_header {
	guint8 command_id;
	guint8 is_response : 1;
	guint8 reserved : 7;
	guint8 reserved2[2];
} __attribute__((packed));

struct gsc_fwu_heci_response {
	struct gsc_fwu_heci_header header;
	guint32 status;
	guint32 reserved;
} __attribute__((packed));

struct gsc_fwu_heci_version_req {
	struct gsc_fwu_heci_header header;
	guint32 partition;
} __attribute__((packed));

struct gsc_fwu_heci_version_resp {
	struct gsc_fwu_heci_response response;
	guint32 partition;
	guint32 version_length;
	guint8 version[];
} __attribute__((packed));

struct gsc_fw_data_heci_version_req {
	struct gsc_fwu_heci_header header;
	guint32 reserved[2];
} __attribute__((packed));

struct gsc_fw_data_heci_version_resp {
	struct gsc_fwu_heci_response response;
	guint32 format_version;
	guint32 oem_version_nvm;
	guint32 oem_version_fitb;
	guint16 major_version;
	guint16 major_vcn;
	guint32 oem_version_fitb_valid;
	guint32 flags;
	guint32 reserved[7];
} __attribute__((packed));

struct gsc_fwu_heci_get_config_message_req {
	struct gsc_fwu_heci_header header;
	guint32 reserved[2];
} __attribute__((packed));

struct gsc_fwu_heci_get_config_message_resp {
	struct gsc_fwu_heci_response response;
	guint32 format_version;
	guint32 hw_step;
	guint32 hw_sku;
	guint32 oprom_code_devid_enforcement : 1;
	guint32 flags : 31;
	guint32 reserved[7];
	guint32 debug_config;
} __attribute__((packed));

struct gsc_fwu_heci_get_subsystem_ids_message_req {
	struct gsc_fwu_heci_header header;
	guint32 reserved[2];
} __attribute__((packed));

struct gsc_fwu_heci_get_subsystem_ids_message_resp {
	struct gsc_fwu_heci_response response;
	guint16 ssvid;
	guint16 ssdid;
	guint32 reserved[2];
} __attribute__((packed));

struct gsc_fwu_heci_start_flags {
	guint32 force_update : 1;
	guint32 reserved : 31;
} __attribute__((packed));

struct gsc_fwu_heci_start_req {
	struct gsc_fwu_heci_header header;
	guint32 update_img_length;
	guint32 payload_type;
	struct gsc_fwu_heci_start_flags flags;
	guint32 reserved[8];
} __attribute__((packed));

struct gsc_fwu_heci_start_resp {
	struct gsc_fwu_heci_response response;
} __attribute__((packed));

struct gsc_fwu_heci_data_req {
	struct gsc_fwu_heci_header header;
	guint32 data_length;
	guint32 reserved;
} __attribute__((packed));

struct gsc_fwu_heci_data_resp {
	struct gsc_fwu_heci_response response;
} __attribute__((packed));

struct gsc_fwu_heci_end_req {
	struct gsc_fwu_heci_header header;
	guint32 reserved;
} __attribute__((packed));

struct gsc_fwu_heci_end_resp {
	struct gsc_fwu_heci_response response;
} __attribute__((packed));

struct gsc_fwu_heci_no_update_req {
	struct gsc_fwu_heci_header header;
	guint32 reserved;
} __attribute__((packed));

struct gsc_fwu_heci_image_metadata {
	guint32 metadata_format_version;
} __attribute__((packed));
