/*
 * Copyright (C) 2020 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include "fu-mei-common.h"

const gchar *
fu_mei_common_family_to_string(FuMeiFamily family)
{
	if (family == FU_MEI_FAMILY_SPS)
		return "SPS";
	if (family == FU_MEI_FAMILY_TXE)
		return "TXE";
	if (family == FU_MEI_FAMILY_ME)
		return "ME";
	if (family == FU_MEI_FAMILY_CSME)
		return "CSME";
	return "AMT";
}

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
	if (vers->major == 11 && (vers->minor == 8 || vers->minor == 11 || vers->minor == 22)) {
		return vers->hotfix >= 70 ? FU_MEI_ISSUE_PATCHED : FU_MEI_ISSUE_VULNERABLE;
	} else if (vers->major == 12 && vers->minor == 0) {
		return (vers->hotfix == 49 || vers->hotfix >= 56) ? FU_MEI_ISSUE_PATCHED
								  : FU_MEI_ISSUE_VULNERABLE;
	} else if (vers->major == 13 && vers->minor == 0) {
		return vers->hotfix >= 21 ? FU_MEI_ISSUE_PATCHED : FU_MEI_ISSUE_VULNERABLE;
	} else if (vers->major == 14 && vers->minor == 0) {
		return vers->hotfix >= 11 ? FU_MEI_ISSUE_PATCHED : FU_MEI_ISSUE_VULNERABLE;
	}
	return FU_MEI_ISSUE_NOT_VULNERABLE;
}

FuMeiIssue
fu_mei_common_is_txe_vulnerable(FuMeiVersion *vers)
{
	if (vers->major == 3 && vers->minor == 1)
		return vers->hotfix >= 70 ? FU_MEI_ISSUE_PATCHED : FU_MEI_ISSUE_VULNERABLE;
	if (vers->major == 4 && vers->minor == 0)
		return vers->hotfix >= 20 ? FU_MEI_ISSUE_PATCHED : FU_MEI_ISSUE_VULNERABLE;
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

/* HFS1[3:0] Current Working State Values */
static const char *me_cws_values[] = {
    [ME_HFS_CWS_RESET] = "reset",
    [ME_HFS_CWS_INIT] = "initializing",
    [ME_HFS_CWS_REC] = "recovery",
    [ME_HFS_CWS_TEST] = "test",
    [ME_HFS_CWS_DISABLED] = "disabled",
    [ME_HFS_CWS_NORMAL] = "normal",
    [ME_HFS_CWS_WAIT] = "wait",
    [ME_HFS_CWS_TRANS] = "transition",
    [ME_HFS_CWS_INVALID] = "invalid",
};

/* HFS1[8:6] Current Operation State Values */
static const char *me_opstate_values[] = {
    [ME_HFS_STATE_PREBOOT] = "preboot",
    [ME_HFS_STATE_M0_UMA] = "m0-with-uma",
    [ME_HFS_STATE_M3] = "m3-without-uma",
    [ME_HFS_STATE_M0] = "m0-without-uma",
    [ME_HFS_STATE_BRINGUP] = "bring-up",
    [ME_HFS_STATE_ERROR] = "error",
};

/* HFS[19:16] Current Operation Mode Values */
static const char *me_opmode_values[] = {
    [ME_HFS_MODE_NORMAL] = "normal",
    [ME_HFS_MODE_DEBUG] = "debug",
    [ME_HFS_MODE_DIS] = "disable",
    [ME_HFS_MODE_OVER_JMPR] = "override-jumper",
    [ME_HFS_MODE_OVER_MEI] = "override-mei",
    [ME_HFS_MODE_UNKNOWN_6] = "unknown-6",
    [ME_HFS_MODE_MAYBE_SPS] = "maybe-sps",
};

/* HFS[15:12] Error Code Values */
static const char *me_error_values[] = {
    [ME_HFS_ERROR_NONE] = "no-error",
    [ME_HFS_ERROR_UNCAT] = "uncategorized-failure",
    [ME_HFS_ERROR_DISABLED] = "disabled",
    [ME_HFS_ERROR_IMAGE] = "image-failure",
    [ME_HFS_ERROR_DEBUG] = "debug-failure",
};

void
fu_mei_hfsts1_to_string(FuMeiHfsts1 hfsts1, guint idt, GString *str)
{
	fu_string_append(str, idt, "WorkingState", me_cws_values[hfsts1.fields.working_state]);
	fu_string_append_kb(str, idt, "MfgMode", hfsts1.fields.mfg_mode);
	fu_string_append_kb(str, idt, "FptBad", hfsts1.fields.fpt_bad);
	fu_string_append(str,
			 idt,
			 "OperationState",
			 me_opstate_values[hfsts1.fields.operation_state]);
	fu_string_append_kb(str, idt, "FwInitComplete", hfsts1.fields.fw_init_complete);
	fu_string_append_kb(str, idt, "FtBupLdFlr", hfsts1.fields.ft_bup_ld_flr);
	fu_string_append_kb(str, idt, "UpdateInProgress", hfsts1.fields.update_in_progress);
	fu_string_append(str, idt, "ErrorCode", me_error_values[hfsts1.fields.error_code]);
	fu_string_append(str, idt, "OperationMode", me_opmode_values[hfsts1.fields.operation_mode]);
	fu_string_append_kx(str, idt, "ResetCount", hfsts1.fields.reset_count);
	fu_string_append_kb(str, idt, "BootOptions_present", hfsts1.fields.boot_options_present);
	fu_string_append_kb(str, idt, "BistFinished", hfsts1.fields.bist_finished);
	fu_string_append_kb(str, idt, "BistTestState", hfsts1.fields.bist_test_state);
	fu_string_append_kb(str, idt, "BistResetRequest", hfsts1.fields.bist_reset_request);
	fu_string_append_kx(str, idt, "CurrentPowerSource", hfsts1.fields.current_power_source);
	fu_string_append_kb(str, idt, "D3SupportValid", hfsts1.fields.d3_support_valid);
	fu_string_append_kb(str, idt, "D0i3SupportValid", hfsts1.fields.d0i3_support_valid);
}

void
fu_mei_hfsts2_to_string(FuMeiHfsts2 hfsts2, guint idt, GString *str)
{
	fu_string_append_kb(str, idt, "NftpLoadFailure", hfsts2.fields.nftp_load_failure);
	fu_string_append_kx(str, idt, "IccProgStatus", hfsts2.fields.icc_prog_status);
	fu_string_append_kb(str, idt, "InvokeMebx", hfsts2.fields.invoke_mebx);
	fu_string_append_kb(str, idt, "CpuReplaced", hfsts2.fields.cpu_replaced);
	fu_string_append_kb(str, idt, "Rsvd0", hfsts2.fields.rsvd0);
	fu_string_append_kb(str, idt, "MfsFailure", hfsts2.fields.mfs_failure);
	fu_string_append_kb(str, idt, "WarmResetRqst", hfsts2.fields.warm_reset_rqst);
	fu_string_append_kb(str, idt, "CpuReplacedValid", hfsts2.fields.cpu_replaced_valid);
	fu_string_append_kb(str, idt, "LowPowerState", hfsts2.fields.low_power_state);
	fu_string_append_kb(str, idt, "MePowerGate", hfsts2.fields.me_power_gate);
	fu_string_append_kb(str, idt, "IpuNeeded", hfsts2.fields.ipu_needed);
	fu_string_append_kb(str, idt, "ForcedSafeBoot", hfsts2.fields.forced_safe_boot);
	fu_string_append_kx(str, idt, "Rsvd1", hfsts2.fields.rsvd1);
	fu_string_append_kb(str, idt, "ListenerChange", hfsts2.fields.listener_change);
	fu_string_append_kx(str, idt, "StatusData", hfsts2.fields.status_data);
	fu_string_append_kx(str, idt, "CurrentPmevent", hfsts2.fields.current_pmevent);
	fu_string_append_kx(str, idt, "Phase", hfsts2.fields.phase);
}

void
fu_mei_hfsts3_to_string(FuMeiHfsts3 hfsts3, guint idt, GString *str)
{
	fu_string_append_kx(str, idt, "Chunk0", hfsts3.fields.chunk0);
	fu_string_append_kx(str, idt, "Chunk1", hfsts3.fields.chunk1);
	fu_string_append_kx(str, idt, "Chunk2", hfsts3.fields.chunk2);
	fu_string_append_kx(str, idt, "Chunk3", hfsts3.fields.chunk3);
	fu_string_append_kx(str, idt, "FwSku", hfsts3.fields.fw_sku);
	fu_string_append_kb(str, idt, "EncryptKeyCheck", hfsts3.fields.encrypt_key_check);
	fu_string_append_kb(str, idt, "PchConfigChange", hfsts3.fields.pch_config_change);
	fu_string_append_kb(str,
			    idt,
			    "IbbVerificationResult",
			    hfsts3.fields.ibb_verification_result);
	fu_string_append_kb(str, idt, "IbbVerificationDone", hfsts3.fields.ibb_verification_done);
	fu_string_append_kx(str, idt, "Reserved11", hfsts3.fields.reserved_11);
	fu_string_append_kx(str, idt, "ActualIbbSize", hfsts3.fields.actual_ibb_size * 1024);
	fu_string_append_ku(str, idt, "NumberOfChunks", hfsts3.fields.number_of_chunks);
	fu_string_append_kb(str, idt, "EncryptKeyOverride", hfsts3.fields.encrypt_key_override);
	fu_string_append_kb(str, idt, "PowerDownMitigation", hfsts3.fields.power_down_mitigation);
}

void
fu_mei_hfsts4_to_string(FuMeiHfsts4 hfsts4, guint idt, GString *str)
{
	fu_string_append_kx(str, idt, "Rsvd0", hfsts4.fields.rsvd0);
	fu_string_append_kb(str, idt, "EnforcementFlow", hfsts4.fields.enforcement_flow);
	fu_string_append_kb(str, idt, "SxResumeType", hfsts4.fields.sx_resume_type);
	fu_string_append_kb(str, idt, "Rsvd1", hfsts4.fields.rsvd1);
	fu_string_append_kb(str, idt, "TpmsDisconnected", hfsts4.fields.tpms_disconnected);
	fu_string_append_kb(str, idt, "Rvsd2", hfsts4.fields.rvsd2);
	fu_string_append_kb(str, idt, "FwstsValid", hfsts4.fields.fwsts_valid);
	fu_string_append_kb(str, idt, "BootGuardSelfTest", hfsts4.fields.boot_guard_self_test);
	fu_string_append_kx(str, idt, "Rsvd3", hfsts4.fields.rsvd3);
}

void
fu_mei_hfsts5_to_string(FuMeiHfsts5 hfsts5, guint idt, GString *str)
{
	fu_string_append_kb(str, idt, "AcmActive", hfsts5.fields.acm_active);
	fu_string_append_kb(str, idt, "Valid", hfsts5.fields.valid);
	fu_string_append_kb(str, idt, "ResultCodeSource", hfsts5.fields.result_code_source);
	fu_string_append_kx(str, idt, "ErrorStatusCode", hfsts5.fields.error_status_code);
	fu_string_append_kx(str, idt, "AcmDoneSts", hfsts5.fields.acm_done_sts);
	fu_string_append_kx(str, idt, "TimeoutCount", hfsts5.fields.timeout_count);
	fu_string_append_kb(str, idt, "ScrtmIndicator", hfsts5.fields.scrtm_indicator);
	fu_string_append_kx(str, idt, "IncBootGuardAcm", hfsts5.fields.inc_boot_guard_acm);
	fu_string_append_kx(str, idt, "IncKeyManifest", hfsts5.fields.inc_key_manifest);
	fu_string_append_kx(str, idt, "IncBootPolicy", hfsts5.fields.inc_boot_policy);
	fu_string_append_kx(str, idt, "Rsvd0", hfsts5.fields.rsvd0);
	fu_string_append_kb(str, idt, "StartEnforcement", hfsts5.fields.start_enforcement);
}

void
fu_mei_hfsts6_to_string(FuMeiHfsts6 hfsts6, guint idt, GString *str)
{
	fu_string_append_kb(str, idt, "ForceBootGuardAcm", hfsts6.fields.force_boot_guard_acm);
	fu_string_append_kb(str, idt, "CpuDebugDisable", hfsts6.fields.cpu_debug_disable);
	fu_string_append_kb(str, idt, "BspInitDisable", hfsts6.fields.bsp_init_disable);
	fu_string_append_kb(str, idt, "ProtectBiosEnv", hfsts6.fields.protect_bios_env);
	fu_string_append_kx(str, idt, "Rsvd0", hfsts6.fields.rsvd0);
	fu_string_append_kx(str, idt, "ErrorEnforcePolicy", hfsts6.fields.error_enforce_policy);
	fu_string_append_kb(str, idt, "MeasuredBoot", hfsts6.fields.measured_boot);
	fu_string_append_kb(str, idt, "VerifiedBoot", hfsts6.fields.verified_boot);
	fu_string_append_kx(str, idt, "BootGuardAcmsvn", hfsts6.fields.boot_guard_acmsvn);
	fu_string_append_kx(str, idt, "Kmsvn", hfsts6.fields.kmsvn);
	fu_string_append_kx(str, idt, "Bpmsvn", hfsts6.fields.bpmsvn);
	fu_string_append_kx(str, idt, "KeyManifestId", hfsts6.fields.key_manifest_id);
	fu_string_append_kb(str, idt, "BootPolicyStatus", hfsts6.fields.boot_policy_status);
	fu_string_append_kb(str, idt, "Error", hfsts6.fields.error);
	fu_string_append_kb(str, idt, "BootGuardDisable", hfsts6.fields.boot_guard_disable);
	fu_string_append_kb(str, idt, "FpfDisable", hfsts6.fields.fpf_disable);
	fu_string_append_kb(str, idt, "FpfSocLock", hfsts6.fields.fpf_soc_lock);
	fu_string_append_kb(str, idt, "TxtSupport", hfsts6.fields.txt_support);
}
