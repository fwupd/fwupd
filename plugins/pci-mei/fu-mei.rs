// Copyright (C) 2023 Richard Hughes <richard@hughsie.com>
// SPDX-License-Identifier: LGPL-2.1+

#[derive(ToString)]
enum FuMeiFamily {
    Unknown,
    Sps,
    Txe,
    Me,
    Csme,
}

# HFS1[3:0] Current Working State Values
#[derive(ToString)]
enum FuMeHfsCws {
    Reset,
    Initializing,
    Recovery,
    Test,
    Disabled,
    Normal,
    Wait,
    Transition,
    Invalid,
}

# HFS1[8:6] Current Operation State Values
#[derive(ToString)]
enum FuMeHfsState {
    Preboot,
    M0WithUma = 1,
    M3WithoutUma = 4,
    M0WithoutUma = 5,
    BringUp = 6,
    Error = 7,
}

# HFS[19:16] Current Operation Mode Values
#[derive(ToString)]
enum FuMeHfsMode {
    Normal,
    Debug = 2,
    Disable,
    OverrideJumper,
    OverrideMei,
    Unknown6,
    MaybeSps,
}

# HFS[15:12] Error Code Values
#[derive(ToString)]
enum FuMeHfsError {
    NoError,
    UncategorizedFailure,
    Disabled,
    ImageFailure,
    DebugFailure,
}

enum FuMeHfsEnforcementPolicy {
    Nothing,
    ShutdownTo,
    ShutdownNow,
    Shutdown_30mins,
}
