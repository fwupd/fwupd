// Copyright (C) 2023 Richard Hughes <richard@hughsie.com>
// SPDX-License-Identifier: LGPL-2.1+

#[derive(ToString)]
enum FuMeiFamily {
    Unknown,
    Sps,
    Txe,
    Me,
    Csme, // 11 to 17
    Csme18,
}

#[derive(ToString)]
enum FuMeiIssue {
    Unknown,
    NotVulnerable,
    Vulnerable,
    Patched,
}

# HFS1 Current Working State Values
#[repr(u4)]
enum FuMeHfsCws {
    Reset,
    Initializing,
    Recovery,
    Test,
    Disabled,
    Normal,
    Wait,
    Transition,
    InvalidCpu,
    Halt = 0x0E,
}

# HFS1 Current Operation State Values
#[repr(u3)]
enum FuMeHfsState {
    Preboot,
    M0WithUma,
    M0PowerGated,
    _Reserved,
    M3WithoutUma,
    M0WithoutUma,
    BringUp,
    Error,
}

# HFS Current Operation Mode Values
#[repr(u4)]
enum FuMeHfsMode {
    Normal,
    _Reserved,
    Debug,
    Disable,
    OverrideJumper,
    OverrideMei,
    Unknown6,
    EnhancedDebug,
}

# HFS Error Code Values
#[repr(u4)]
enum FuMeHfsError {
    NoError,
    UncategorizedFailure,
    Disabled,
    ImageFailure,
    DebugFailure,
}

#[repr(u2)]
enum FuMeHfsEnforcementPolicy {
    Nothing,
    ShutdownTo,
    ShutdownNow,
    Shutdown_30mins,
}

#[repr(u3)]
enum FuMeiFirmwareSku {
    Consumer = 0x02,
    Corporate = 0x03,
    Lite = 0x05,
}

/* CSME11 - Host Firmware Status register 1 */
#[derive(Parse)]
struct FuMeiCsme11Hfsts1 {
    _working_state: FuMeHfsCws,
    mfg_mode: u1,
    _fpt_bad: u1,
    _operation_state: FuMeHfsState,
    _fw_init_complete: u1,
    _ft_bup_ld_flr: u1,
    _update_in_progress: u1,
    _error_code: FuMeHfsError,
    operation_mode: FuMeHfsMode,
    _reset_count: u4,
    _boot_options_present: u1,
    _bist_finished: u1,
    _bist_test_state: u1,
    _bist_reset_request: u1,
    _current_power_source: u2,
    _d3_support_valid: u1,
    _d0i3_support_valid: u1,
}

/* CSME11 - Host Firmware Status register 2 */
struct FuMeiCsme11Hfsts2 {
    nftp_load_failure: u1,
    icc_prog_status: u2,
    invoke_mebx: u1,
    cpu_replaced: u1,
    rsvd0: u1,
    mfs_failure: u1,
    warm_reset_rqst: u1,
    cpu_replaced_valid: u1,
    low_power_state: u1,
    me_power_gate: u1,
    ipu_needed: u1,
    forced_safe_boot: u1,
    rsvd1: u2,
    listener_change: u1,
    status_data: u8,
    current_pmevent: u4,
    phase: u4,
}

/* CSME11 - Host Firmware Status register 3 */
struct FuMeiCsme11Hfsts3 {
    chunk0: u1,
    chunk1: u1,
    chunk2: u1,
    chunk3: u1,
    fw_sku: FuMeiFirmwareSku,
    encrypt_key_check: u1,
    pch_config_change: u1,
    ibb_verification_result: u1,
    ibb_verification_done: u1,
    reserved_11: u3,
    actual_ibb_size: u14,
    number_of_chunks: u2,
    encrypt_key_override: u1,
    power_down_mitigation: u1,
}

/* CSME11 - Host Firmware Status register 4 */
struct FuMeiCsme11Hfsts4 {
    rsvd0: u9,
    enforcement_flow: u1,
    sx_resume_type: u1,
    rsvd1: u1,
    tpms_disconnected: u1,
    rvsd2: u1,
    fwsts_valid: u1,
    boot_guard_self_test: u1,
    rsvd3: u16,
}

/* CSME11 - Host Firmware Status register 5 */
struct FuMeiCsme11Hfsts5 {
    acm_active: u1,
    valid: u1,
    result_code_source: u1,
    error_status_code: u5,
    acm_done_sts: u1,
    timeout_count: u7,
    scrtm_indicator: u1,
    inc_boot_guard_acm: u4,
    inc_key_manifest: u4,
    inc_boot_policy: u4,
    rsvd0: u2,
    start_enforcement: u1,
}

/* CSME11 - Host Firmware Status register 6 */
#[derive(Parse)]
struct FuMeiCsme11Hfsts6 {
    force_boot_guard_acm: u1,
    _cpu_debug_disable: u1,
    _bsp_init_disable: u1,
    _protect_bios_env: u1,
    _rsvd0: u2,
    error_enforce_policy: FuMeHfsEnforcementPolicy,
    _measured_boot: u1,
    verified_boot: u1,
    _boot_guard_acmsvn: u4,
    _kmsvn: u4,
    _bpmsvn: u4,
    _key_manifest_id: u4,
    _boot_policy_status: u1,
    _error: u1,
    boot_guard_disable: u1,
    _fpf_disable: u1,
    fpf_soc_lock: u1,
    _txt_support: u1,
}

/* CSME18 - Host Firmware Status register 1 */
#[derive(Parse)]
struct FuMeiCsme18Hfsts1 {
    _working_state: FuMeHfsCws,
    spi_protection_mode: u1,
    _fpt_bad: u1,
    _operation_state: FuMeHfsState,
    _fw_init_complete: u1,
    _ft_bup_ld_flr: u1,
    _update_in_progress: u1,
    _error_code: FuMeHfsError,
    operation_mode: FuMeHfsMode,
    _reset_count: u4,
    _boot_options_present: u1,
    _invoke_enhanced_debug_mode: u1,
    _bist_test_state: u1,
    _bist_reset_request: u1,
    _current_power_source: u2,
    _d3_support_valid: u1,
    _d0i3_support_valid: u1,
}


/* CSME18 - Host Firmware Status register 2 */
struct FuMeiCsme18Hfsts2 {
    nftp_load_failure: u1,
    icc_prog_status: u2,
    invoke_mebx: u1,
    cpu_replaced: u1,
    rsvd0: u1,
    mfs_failure: u1,
    warm_reset_rqst: u1,
    cpu_replaced_valid: u1,
    low_power_state: u1,
    me_power_gate: u1,
    ipu_needed: u1,
    rsvd1: u2,
    cse_way_to_disabled: u1,
    listener_change: u1,
    status_data: u8,
    current_pmevent: u4,
    phase: u4,
}

/* CSME18 - Host Firmware Status register 3 */
struct FuMeiCsme18Hfsts3 {
    reserved: u4,
    fw_sku: FuMeiFirmwareSku,
    transactional_state: u1,
    storage_proxy_present: u1,
    reserved: u2,
    rpmc_status_values: u4,
    rpmc_device_extended_status: u3,
    bios_rpmc_status: u4,
    bios_rpmc_device_extended_status: u3,
    reserved: u7,
}

/* CSME18 - Host Firmware Status register 4 */
struct FuMeiCsme18Hfsts4 {
    rsvd0: u2,
    flash_log_exist: u1,
    rsvd1: u29,
}

#[repr(u5)]
enum FuMeiCsme18ErrorStatusCode {
    Success,
    BootGuardInitializationFailed,
    KmVerificationFailed,
    BpmVerificationFailed,
    IbbVerificationFailed,
    FitProcessingFailed,
    DmaSetupFailed,
    NemSetupFailed,
    TpmSetupFailed,
    IbbMeasurementFailed,
    _Unknown,
    MeConnectionFailed,
}

/* CSME18 - Host Firmware Status register 5 */
#[derive(Parse)]
struct FuMeiCsme18Hfsts5 {
    btg_acm_active: u1,
    valid: u1,
    _result_code_source: u1,
    _error_status_code: FuMeiCsme18ErrorStatusCode,
    acm_done_sts: u1,
    _timeout_count: u7,
    _scrtm_indicator: u1,
    _txt_support: u1,
    _btg_profile: u3,
    _cpu_debug_disabled: u1,
    _bsp_init_disabled: u1,
    _bsp_boot_policy_manifest_execution_status: u1,
    _btg_token_applied: u1,
    _btg_status: u4,
    _reserved: u2,
    _start_enforcement: u1,
}

/* CSME18 - Host Firmware Status register 6 */
#[derive(Parse)]
struct FuMeiCsme18Hfsts6 {
    _reserved0: u21,
    manufacturing_lock: u1,
    _reserved1: u8,
    fpf_soc_configuration_lock: u1,
    _sx_resume_type: u1,
}
