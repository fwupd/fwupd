/*
 * Copyright 2022 Intel
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
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

struct gsc_fwu_heci_header {
	guint8 command_id;
	guint8 is_response : 1;
	guint8 reserved : 7;
	guint8 reserved2[2];
} __attribute__((packed)); /* nocheck:blocked */

struct gsc_fwu_heci_response {
	struct gsc_fwu_heci_header header;
	guint32 status;
	guint32 reserved;
} __attribute__((packed)); /* nocheck:blocked */

struct gsc_fwu_heci_version_req {
	struct gsc_fwu_heci_header header;
	guint32 partition;
} __attribute__((packed)); /* nocheck:blocked */

struct gsc_fwu_heci_version_resp {
	struct gsc_fwu_heci_response response;
	guint32 partition;
	guint32 version_length;
	guint8 version[];
} __attribute__((packed)); /* nocheck:blocked */

struct gsc_fw_data_heci_version_req {
	struct gsc_fwu_heci_header header;
	guint32 reserved[2];
} __attribute__((packed)); /* nocheck:blocked */

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
} __attribute__((packed)); /* nocheck:blocked */

struct gsc_fwu_heci_get_config_message_req {
	struct gsc_fwu_heci_header header;
	guint32 reserved[2];
} __attribute__((packed)); /* nocheck:blocked */

struct gsc_fwu_heci_get_config_message_resp {
	struct gsc_fwu_heci_response response;
	guint32 format_version;
	guint32 hw_step;
	guint32 hw_sku;
	guint32 oprom_code_devid_enforcement : 1;
	guint32 flags : 31;
	guint32 reserved[7];
	guint32 debug_config;
} __attribute__((packed)); /* nocheck:blocked */

struct gsc_fwu_heci_get_subsystem_ids_message_req {
	struct gsc_fwu_heci_header header;
	guint32 reserved[2];
} __attribute__((packed)); /* nocheck:blocked */

struct gsc_fwu_heci_get_subsystem_ids_message_resp {
	struct gsc_fwu_heci_response response;
	guint16 ssvid;
	guint16 ssdid;
	guint32 reserved[2];
} __attribute__((packed)); /* nocheck:blocked */

struct gsc_fwu_heci_start_flags {
	guint32 force_update : 1;
	guint32 reserved : 31;
} __attribute__((packed)); /* nocheck:blocked */

struct gsc_fwu_heci_start_req {
	struct gsc_fwu_heci_header header;
	guint32 update_img_length;
	guint32 payload_type;
	struct gsc_fwu_heci_start_flags flags;
	guint32 reserved[8];
} __attribute__((packed)); /* nocheck:blocked */

struct gsc_fwu_heci_start_resp {
	struct gsc_fwu_heci_response response;
} __attribute__((packed)); /* nocheck:blocked */

struct gsc_fwu_heci_data_req {
	struct gsc_fwu_heci_header header;
	guint32 data_length;
	guint32 reserved;
} __attribute__((packed)); /* nocheck:blocked */

struct gsc_fwu_heci_data_resp {
	struct gsc_fwu_heci_response response;
} __attribute__((packed)); /* nocheck:blocked */

struct gsc_fwu_heci_end_req {
	struct gsc_fwu_heci_header header;
	guint32 reserved;
} __attribute__((packed)); /* nocheck:blocked */

struct gsc_fwu_heci_end_resp {
	struct gsc_fwu_heci_response response;
} __attribute__((packed)); /* nocheck:blocked */

struct gsc_fwu_heci_no_update_req {
	struct gsc_fwu_heci_header header;
	guint32 reserved;
} __attribute__((packed)); /* nocheck:blocked */
