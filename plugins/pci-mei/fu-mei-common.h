/*
 * Copyright (C) 2020 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#pragma once

#include <fwupdplugin.h>

typedef enum {
	FU_MEI_FAMILY_UNKNOWN,
	FU_MEI_FAMILY_SPS,
	FU_MEI_FAMILY_TXE,
	FU_MEI_FAMILY_ME,
	FU_MEI_FAMILY_CSME,
} FuMeiFamily;

typedef enum {
	FU_MEI_ISSUE_UNKNOWN,
	FU_MEI_ISSUE_NOT_VULNERABLE,
	FU_MEI_ISSUE_VULNERABLE,
	FU_MEI_ISSUE_PATCHED,
} FuMeiIssue;

typedef struct {
	guint8	platform;
	guint8	major;
	guint8	minor;
	guint8	hotfix;
	guint16	buildno;
} FuMeiVersion;

/* Host Firmware Status register 1 */
typedef union {
	guint32 data;
	struct {
		guint32 working_state		: 4;
		guint32 mfg_mode		: 1;
		guint32 fpt_bad			: 1;
		guint32 operation_state		: 3;
		guint32 fw_init_complete	: 1;
		guint32 ft_bup_ld_flr		: 1;
		guint32 update_in_progress	: 1;
		guint32 error_code		: 4;
		guint32 operation_mode		: 4;
		guint32 reset_count		: 4;
		guint32 boot_options_present	: 1;
		guint32 bist_finished		: 1;
		guint32 bist_test_state		: 1;
		guint32 bist_reset_request	: 1;
		guint32 current_power_source	: 2;
		guint32 d3_support_valid	: 1;
		guint32 d0i3_support_valid	: 1;
	} __attribute__((packed)) fields;
} FuMeiHfsts1;

/* Host Firmware Status Register 2 */
typedef union {
	guint32 data;
	struct {
		guint32 nftp_load_failure	: 1;
		guint32 icc_prog_status		: 2;
		guint32 invoke_mebx		: 1;
		guint32 cpu_replaced		: 1;
		guint32 rsvd0			: 1;
		guint32 mfs_failure		: 1;
		guint32 warm_reset_rqst		: 1;
		guint32 cpu_replaced_valid	: 1;
		guint32 low_power_state		: 1;
		guint32 me_power_gate		: 1;
		guint32 ipu_needed		: 1;
		guint32 forced_safe_boot	: 1;
		guint32 rsvd1			: 2;
		guint32 listener_change		: 1;
		guint32 status_data		: 8;
		guint32 current_pmevent		: 4;
		guint32 phase			: 4;
	} __attribute__((packed)) fields;
} FuMeiHfsts2;

/* Host Firmware Status Register 3 */
typedef union {
	guint32 data;
	struct {
		guint32 chunk0			: 1;
		guint32 chunk1			: 1;
		guint32 chunk2			: 1;
		guint32 chunk3			: 1;
		guint32 fw_sku			: 3;
		guint32 encrypt_key_check	: 1;
		guint32 pch_config_change	: 1;
		guint32 ibb_verification_result	: 1;
		guint32 ibb_verification_done	: 1;
		guint32 reserved_11		: 3;
		guint32 actual_ibb_size		: 14;
		guint32 number_of_chunks	: 2;
		guint32 encrypt_key_override	: 1;
		guint32 power_down_mitigation	: 1;
	} __attribute__((packed)) fields;
} FuMeiHfsts3;

/* Host Firmware Status Register 4 */
typedef union {
	guint32 data;
	struct {
		guint32 rsvd0			: 9;
		guint32 enforcement_flow	: 1;
		guint32 sx_resume_type		: 1;
		guint32 rsvd1			: 1;
		guint32 tpms_disconnected	: 1;
		guint32 rvsd2			: 1;
		guint32 fwsts_valid		: 1;
		guint32 boot_guard_self_test	: 1;
		guint32 rsvd3			: 16;
	} __attribute__((packed)) fields;
} FuMeiHfsts4;

/* Host Firmware Status Register 5 */
typedef union {
	guint32 data;
	struct {
		guint32 acm_active		: 1;
		guint32 valid			: 1;
		guint32 result_code_source	: 1;
		guint32 error_status_code	: 5;
		guint32 acm_done_sts		: 1;
		guint32 timeout_count		: 7;
		guint32 scrtm_indicator		: 1;
		guint32 inc_boot_guard_acm	: 4;
		guint32 inc_key_manifest	: 4;
		guint32 inc_boot_policy		: 4;
		guint32 rsvd0			: 2;
		guint32 start_enforcement	: 1;
	} __attribute__((packed)) fields;
} FuMeiHfsts5;

/* Host Firmware Status Register 6 */
typedef union {
	guint32 data;
	struct {
		guint32 force_boot_guard_acm	: 1;
		guint32 cpu_debug_disable	: 1;
		guint32 bsp_init_disable	: 1;
		guint32 protect_bios_env	: 1;
		guint32 rsvd0			: 2;
		guint32 error_enforce_policy	: 2;
		guint32 measured_boot		: 1;
		guint32 verified_boot		: 1;
		guint32 boot_guard_acmsvn	: 4;
		guint32 kmsvn			: 4;
		guint32 bpmsvn			: 4;
		guint32 key_manifest_id		: 4;
		guint32 boot_policy_status	: 1;
		guint32 error			: 1;
		guint32 boot_guard_disable	: 1;
		guint32 fpf_disable		: 1;
		guint32 fpf_soc_lock		: 1;
		guint32 txt_support		: 1;
	} __attribute__((packed)) fields;
} FuMeiHfsts6;

#define ME_HFS_CWS_RESET	0
#define ME_HFS_CWS_INIT		1
#define ME_HFS_CWS_REC		2
#define ME_HFS_CWS_TEST		3
#define ME_HFS_CWS_DISABLED	4
#define ME_HFS_CWS_NORMAL	5
#define ME_HFS_CWS_WAIT		6
#define ME_HFS_CWS_TRANS	7
#define ME_HFS_CWS_INVALID	8

#define ME_HFS_STATE_PREBOOT	0
#define ME_HFS_STATE_M0_UMA	1
#define ME_HFS_STATE_M3		4
#define ME_HFS_STATE_M0		5
#define ME_HFS_STATE_BRINGUP	6
#define ME_HFS_STATE_ERROR	7

#define ME_HFS_ERROR_NONE	0
#define ME_HFS_ERROR_UNCAT	1
#define ME_HFS_ERROR_DISABLED	2
#define ME_HFS_ERROR_IMAGE	3
#define ME_HFS_ERROR_DEBUG	4

#define ME_HFS_MODE_NORMAL	0
#define ME_HFS_MODE_DEBUG	2
#define ME_HFS_MODE_DIS		3
#define ME_HFS_MODE_OVER_JMPR	4
#define ME_HFS_MODE_OVER_MEI	5
#define ME_HFS_MODE_UNKNOWN_6	6
#define ME_HFS_MODE_MAYBE_SPS	7

#define ME_HFS_ENFORCEMENT_POLICY_NOTHING	0b00
#define ME_HFS_ENFORCEMENT_POLICY_SHUTDOWN_TO	0b01
#define ME_HFS_ENFORCEMENT_POLICY_SHUTDOWN_NOW	0b11

const gchar	*fu_mei_common_family_to_string		(FuMeiFamily	 family);
FuMeiIssue	 fu_mei_common_is_csme_vulnerable	(FuMeiVersion	*vers);
FuMeiIssue	 fu_mei_common_is_txe_vulnerable	(FuMeiVersion	*vers);
FuMeiIssue	 fu_mei_common_is_sps_vulnerable	(FuMeiVersion	*vers);

void		 fu_mei_hfsts1_to_string		(FuMeiHfsts1	 hfsts1,
							 guint		 idt,
							 GString	*str);
void		 fu_mei_hfsts2_to_string		(FuMeiHfsts2	 hfsts2,
							 guint		 idt,
							 GString	*str);
void		 fu_mei_hfsts3_to_string		(FuMeiHfsts3	 hfsts3,
							 guint		 idt,
							 GString	*str);
void		 fu_mei_hfsts4_to_string		(FuMeiHfsts4	 hfsts4,
							 guint		 idt,
							 GString	*str);
void		 fu_mei_hfsts5_to_string		(FuMeiHfsts5	 hfsts5,
							 guint		 idt,
							 GString	*str);
void		 fu_mei_hfsts6_to_string		(FuMeiHfsts6	 hfsts6,
							 guint		 idt,
							 GString	*str);
