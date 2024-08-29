/*
 * Copyright 2020 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include "fu-mei-common.h"
#include "fu-mei-struct.h"

static gint
fu_mei_common_cmp_version(FuMeiVersion *vers1, FuMeiVersion *vers2)
{
	guint16 vers1buf[] = {
	    vers1->major,
	    vers1->minor,
	    vers1->hotfix,
	    vers1->buildno,
	};
	guint16 vers2buf[] = {
	    vers2->major,
	    vers2->minor,
	    vers2->hotfix,
	    vers2->buildno,
	};
	for (guint i = 0; i < 4; i++) {
		if (vers1buf[i] < vers2buf[i])
			return -1;
		if (vers1buf[i] > vers2buf[i])
			return 1;
	}
	return 0;
}

FuMeiIssue
fu_mei_common_is_csme_vulnerable(FuMeiVersion *vers)
{
	struct {
		guint8 major_eq;
		guint8 minor_eq;
		guint8 hotfix_ge;
	} verdata[] = {{11, 8, 92},
		       {11, 12, 92},
		       {11, 22, 92},
		       {12, 0, 90},
		       {13, 0, 60},
		       {13, 30, 30},
		       {13, 50, 20},
		       {14, 1, 65},
		       {14, 5, 45},
		       {15, 0, 40},
		       {15, 40, 20},
		       {0, 0, 0}};
	for (guint i = 0; verdata[i].major_eq != 0; i++) {
		if (vers->major == verdata[i].major_eq && vers->minor == verdata[i].minor_eq) {
			return vers->hotfix >= verdata[i].hotfix_ge ? FU_MEI_ISSUE_PATCHED
								    : FU_MEI_ISSUE_VULNERABLE;
		}
	}
	return FU_MEI_ISSUE_NOT_VULNERABLE;
}

FuMeiIssue
fu_mei_common_is_txe_vulnerable(FuMeiVersion *vers)
{
	struct {
		guint8 major_eq;
		guint8 minor_eq;
		guint8 hotfix_ge;
	} verdata[] = {{3, 1, 92}, {4, 0, 45}, {0, 0, 0}};
	for (guint i = 0; verdata[i].major_eq != 0; i++) {
		if (vers->major == verdata[i].major_eq && vers->minor == verdata[i].minor_eq) {
			return vers->hotfix >= verdata[i].hotfix_ge ? FU_MEI_ISSUE_PATCHED
								    : FU_MEI_ISSUE_VULNERABLE;
		}
	}
	return FU_MEI_ISSUE_NOT_VULNERABLE;
}

FuMeiIssue
fu_mei_common_is_sps_vulnerable(FuMeiVersion *vers)
{
	if (vers->major == 3 || vers->major > 5)
		return FU_MEI_ISSUE_NOT_VULNERABLE;
	if (vers->major == 4) {
		if (vers->hotfix < 44)
			return FU_MEI_ISSUE_VULNERABLE;
		if (vers->platform == 0xA) { /* Purley */
			FuMeiVersion ver2 = {
			    .major = 4,
			    .minor = 1,
			    .hotfix = 4,
			    .buildno = 339,
			};
			if (fu_mei_common_cmp_version(vers, &ver2) < 0)
				return FU_MEI_ISSUE_VULNERABLE;
		} else if (vers->platform == 0xE) { /* Bakerville */
			FuMeiVersion ver2 = {
			    .major = 4,
			    .minor = 0,
			    .hotfix = 4,
			    .buildno = 112,
			};
			if (fu_mei_common_cmp_version(vers, &ver2) < 0)
				return FU_MEI_ISSUE_VULNERABLE;
		} else if (vers->platform == 0xB) { /* Harrisonville */
			FuMeiVersion ver2 = {
			    .major = 4,
			    .minor = 0,
			    .hotfix = 4,
			    .buildno = 193,
			};
			if (fu_mei_common_cmp_version(vers, &ver2) < 0)
				return FU_MEI_ISSUE_VULNERABLE;
		} else if (vers->platform == 0x9) { /* Greenlow */
			FuMeiVersion ver2 = {
			    .major = 4,
			    .minor = 1,
			    .hotfix = 4,
			    .buildno = 88,
			};
			if (vers->minor < 1)
				return FU_MEI_ISSUE_NOT_VULNERABLE;
			if (fu_mei_common_cmp_version(vers, &ver2) < 0)
				return FU_MEI_ISSUE_VULNERABLE;
		} else if (vers->platform == 0xD) { /* MonteVista */
			FuMeiVersion ver2 = {
			    .major = 4,
			    .minor = 8,
			    .hotfix = 4,
			    .buildno = 51,
			};
			if (fu_mei_common_cmp_version(vers, &ver2) < 0)
				return FU_MEI_ISSUE_VULNERABLE;
		}
		return FU_MEI_ISSUE_NOT_VULNERABLE;
	}
	if (vers->major == 5) {
		if (vers->platform == 0x10) { /* Mehlow */
			FuMeiVersion ver2 = {5, 1, 3, 89};
			if (fu_mei_common_cmp_version(vers, &ver2) < 0)
				return FU_MEI_ISSUE_VULNERABLE;
		}
		return FU_MEI_ISSUE_NOT_VULNERABLE;
	}
	return FU_MEI_ISSUE_PATCHED;
}

void
fu_mei_hfsts1_to_string(FuMeiHfsts1 hfsts1, guint idt, GString *str)
{
	fwupd_codec_string_append(str,
				  idt,
				  "WorkingState",
				  fu_me_hfs_cws_to_string(hfsts1.fields.working_state));
	fwupd_codec_string_append_bool(str, idt, "MfgMode", hfsts1.fields.mfg_mode);
	fwupd_codec_string_append_bool(str, idt, "FptBad", hfsts1.fields.fpt_bad);
	fwupd_codec_string_append(str,
				  idt,
				  "OperationState",
				  fu_me_hfs_state_to_string(hfsts1.fields.operation_state));
	fwupd_codec_string_append_bool(str, idt, "FwInitComplete", hfsts1.fields.fw_init_complete);
	fwupd_codec_string_append_bool(str, idt, "FtBupLdFlr", hfsts1.fields.ft_bup_ld_flr);
	fwupd_codec_string_append_bool(str,
				       idt,
				       "UpdateInProgress",
				       hfsts1.fields.update_in_progress);
	fwupd_codec_string_append(str,
				  idt,
				  "ErrorCode",
				  fu_me_hfs_error_to_string(hfsts1.fields.error_code));
	fwupd_codec_string_append(str,
				  idt,
				  "OperationMode",
				  fu_me_hfs_mode_to_string(hfsts1.fields.operation_mode));
	fwupd_codec_string_append_hex(str, idt, "ResetCount", hfsts1.fields.reset_count);
	fwupd_codec_string_append_bool(str,
				       idt,
				       "BootOptions_present",
				       hfsts1.fields.boot_options_present);
	fwupd_codec_string_append_bool(str, idt, "BistFinished", hfsts1.fields.bist_finished);
	fwupd_codec_string_append_bool(str, idt, "BistTestState", hfsts1.fields.bist_test_state);
	fwupd_codec_string_append_bool(str,
				       idt,
				       "BistResetRequest",
				       hfsts1.fields.bist_reset_request);
	fwupd_codec_string_append_hex(str,
				      idt,
				      "CurrentPowerSource",
				      hfsts1.fields.current_power_source);
	fwupd_codec_string_append_bool(str, idt, "D3SupportValid", hfsts1.fields.d3_support_valid);
	fwupd_codec_string_append_bool(str,
				       idt,
				       "D0i3SupportValid",
				       hfsts1.fields.d0i3_support_valid);
}

void
fu_mei_hfsts2_to_string(FuMeiHfsts2 hfsts2, guint idt, GString *str)
{
	fwupd_codec_string_append_bool(str,
				       idt,
				       "NftpLoadFailure",
				       hfsts2.fields.nftp_load_failure);
	fwupd_codec_string_append_hex(str, idt, "IccProgStatus", hfsts2.fields.icc_prog_status);
	fwupd_codec_string_append_bool(str, idt, "InvokeMebx", hfsts2.fields.invoke_mebx);
	fwupd_codec_string_append_bool(str, idt, "CpuReplaced", hfsts2.fields.cpu_replaced);
	fwupd_codec_string_append_bool(str, idt, "Rsvd0", hfsts2.fields.rsvd0);
	fwupd_codec_string_append_bool(str, idt, "MfsFailure", hfsts2.fields.mfs_failure);
	fwupd_codec_string_append_bool(str, idt, "WarmResetRqst", hfsts2.fields.warm_reset_rqst);
	fwupd_codec_string_append_bool(str,
				       idt,
				       "CpuReplacedValid",
				       hfsts2.fields.cpu_replaced_valid);
	fwupd_codec_string_append_bool(str, idt, "LowPowerState", hfsts2.fields.low_power_state);
	fwupd_codec_string_append_bool(str, idt, "MePowerGate", hfsts2.fields.me_power_gate);
	fwupd_codec_string_append_bool(str, idt, "IpuNeeded", hfsts2.fields.ipu_needed);
	fwupd_codec_string_append_bool(str, idt, "ForcedSafeBoot", hfsts2.fields.forced_safe_boot);
	fwupd_codec_string_append_hex(str, idt, "Rsvd1", hfsts2.fields.rsvd1);
	fwupd_codec_string_append_bool(str, idt, "ListenerChange", hfsts2.fields.listener_change);
	fwupd_codec_string_append_hex(str, idt, "StatusData", hfsts2.fields.status_data);
	fwupd_codec_string_append_hex(str, idt, "CurrentPmevent", hfsts2.fields.current_pmevent);
	fwupd_codec_string_append_hex(str, idt, "Phase", hfsts2.fields.phase);
}

void
fu_mei_hfsts3_to_string(FuMeiHfsts3 hfsts3, guint idt, GString *str)
{
	fwupd_codec_string_append_hex(str, idt, "Chunk0", hfsts3.fields.chunk0);
	fwupd_codec_string_append_hex(str, idt, "Chunk1", hfsts3.fields.chunk1);
	fwupd_codec_string_append_hex(str, idt, "Chunk2", hfsts3.fields.chunk2);
	fwupd_codec_string_append_hex(str, idt, "Chunk3", hfsts3.fields.chunk3);
	fwupd_codec_string_append_hex(str, idt, "FwSku", hfsts3.fields.fw_sku);
	fwupd_codec_string_append_bool(str,
				       idt,
				       "EncryptKeyCheck",
				       hfsts3.fields.encrypt_key_check);
	fwupd_codec_string_append_bool(str,
				       idt,
				       "PchConfigChange",
				       hfsts3.fields.pch_config_change);
	fwupd_codec_string_append_bool(str,
				       idt,
				       "IbbVerificationResult",
				       hfsts3.fields.ibb_verification_result);
	fwupd_codec_string_append_bool(str,
				       idt,
				       "IbbVerificationDone",
				       hfsts3.fields.ibb_verification_done);
	fwupd_codec_string_append_hex(str, idt, "Reserved11", hfsts3.fields.reserved_11);
	fwupd_codec_string_append_hex(str,
				      idt,
				      "ActualIbbSize",
				      hfsts3.fields.actual_ibb_size * 1024);
	fwupd_codec_string_append_int(str, idt, "NumberOfChunks", hfsts3.fields.number_of_chunks);
	fwupd_codec_string_append_bool(str,
				       idt,
				       "EncryptKeyOverride",
				       hfsts3.fields.encrypt_key_override);
	fwupd_codec_string_append_bool(str,
				       idt,
				       "PowerDownMitigation",
				       hfsts3.fields.power_down_mitigation);
}

void
fu_mei_hfsts4_to_string(FuMeiHfsts4 hfsts4, guint idt, GString *str)
{
	fwupd_codec_string_append_hex(str, idt, "Rsvd0", hfsts4.fields.rsvd0);
	fwupd_codec_string_append_bool(str, idt, "EnforcementFlow", hfsts4.fields.enforcement_flow);
	fwupd_codec_string_append_bool(str, idt, "SxResumeType", hfsts4.fields.sx_resume_type);
	fwupd_codec_string_append_bool(str, idt, "Rsvd1", hfsts4.fields.rsvd1);
	fwupd_codec_string_append_bool(str,
				       idt,
				       "TpmsDisconnected",
				       hfsts4.fields.tpms_disconnected);
	fwupd_codec_string_append_bool(str, idt, "Rvsd2", hfsts4.fields.rvsd2);
	fwupd_codec_string_append_bool(str, idt, "FwstsValid", hfsts4.fields.fwsts_valid);
	fwupd_codec_string_append_bool(str,
				       idt,
				       "BootGuardSelfTest",
				       hfsts4.fields.boot_guard_self_test);
	fwupd_codec_string_append_hex(str, idt, "Rsvd3", hfsts4.fields.rsvd3);
}

void
fu_mei_hfsts5_to_string(FuMeiHfsts5 hfsts5, guint idt, GString *str)
{
	fwupd_codec_string_append_bool(str, idt, "AcmActive", hfsts5.fields.acm_active);
	fwupd_codec_string_append_bool(str, idt, "Valid", hfsts5.fields.valid);
	fwupd_codec_string_append_bool(str,
				       idt,
				       "ResultCodeSource",
				       hfsts5.fields.result_code_source);
	fwupd_codec_string_append_hex(str, idt, "ErrorStatusCode", hfsts5.fields.error_status_code);
	fwupd_codec_string_append_hex(str, idt, "AcmDoneSts", hfsts5.fields.acm_done_sts);
	fwupd_codec_string_append_hex(str, idt, "TimeoutCount", hfsts5.fields.timeout_count);
	fwupd_codec_string_append_bool(str, idt, "ScrtmIndicator", hfsts5.fields.scrtm_indicator);
	fwupd_codec_string_append_hex(str,
				      idt,
				      "IncBootGuardAcm",
				      hfsts5.fields.inc_boot_guard_acm);
	fwupd_codec_string_append_hex(str, idt, "IncKeyManifest", hfsts5.fields.inc_key_manifest);
	fwupd_codec_string_append_hex(str, idt, "IncBootPolicy", hfsts5.fields.inc_boot_policy);
	fwupd_codec_string_append_hex(str, idt, "Rsvd0", hfsts5.fields.rsvd0);
	fwupd_codec_string_append_bool(str,
				       idt,
				       "StartEnforcement",
				       hfsts5.fields.start_enforcement);
}

void
fu_mei_hfsts6_to_string(FuMeiHfsts6 hfsts6, guint idt, GString *str)
{
	fwupd_codec_string_append_bool(str,
				       idt,
				       "ForceBootGuardAcm",
				       hfsts6.fields.force_boot_guard_acm);
	fwupd_codec_string_append_bool(str,
				       idt,
				       "CpuDebugDisable",
				       hfsts6.fields.cpu_debug_disable);
	fwupd_codec_string_append_bool(str, idt, "BspInitDisable", hfsts6.fields.bsp_init_disable);
	fwupd_codec_string_append_bool(str, idt, "ProtectBiosEnv", hfsts6.fields.protect_bios_env);
	fwupd_codec_string_append_hex(str, idt, "Rsvd0", hfsts6.fields.rsvd0);
	fwupd_codec_string_append_hex(str,
				      idt,
				      "ErrorEnforcePolicy",
				      hfsts6.fields.error_enforce_policy);
	fwupd_codec_string_append_bool(str, idt, "MeasuredBoot", hfsts6.fields.measured_boot);
	fwupd_codec_string_append_bool(str, idt, "VerifiedBoot", hfsts6.fields.verified_boot);
	fwupd_codec_string_append_hex(str, idt, "BootGuardAcmsvn", hfsts6.fields.boot_guard_acmsvn);
	fwupd_codec_string_append_hex(str, idt, "Kmsvn", hfsts6.fields.kmsvn);
	fwupd_codec_string_append_hex(str, idt, "Bpmsvn", hfsts6.fields.bpmsvn);
	fwupd_codec_string_append_hex(str, idt, "KeyManifestId", hfsts6.fields.key_manifest_id);
	fwupd_codec_string_append_bool(str,
				       idt,
				       "BootPolicyStatus",
				       hfsts6.fields.boot_policy_status);
	fwupd_codec_string_append_bool(str, idt, "Error", hfsts6.fields.error);
	fwupd_codec_string_append_bool(str,
				       idt,
				       "BootGuardDisable",
				       hfsts6.fields.boot_guard_disable);
	fwupd_codec_string_append_bool(str, idt, "FpfDisable", hfsts6.fields.fpf_disable);
	fwupd_codec_string_append_bool(str, idt, "FpfSocLock", hfsts6.fields.fpf_soc_lock);
	fwupd_codec_string_append_bool(str, idt, "TxtSupport", hfsts6.fields.txt_support);
}
