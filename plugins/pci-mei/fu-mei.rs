enum MeiFamily {
    Unknown,
    Sps,
    Txe,
    Me,
    Csme,
}

# HFS1[3:0] Current Working State Values
enum MeHfsCws {
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
enum MeHfsState {
    Preboot,
    M0WithUma = 1,
    M3WithoutUma = 4,
    M0WithoutUma = 5,
    BringUp = 6,
    Error = 7,
}

# HFS[19:16] Current Operation Mode Values
enum MeHfsMode {
    Normal,
    Debug = 2,
    Disable,
    OverrideJumper,
    OverrideMei,
    Unknown6,
    MaybeSps,
}

# HFS[15:12] Error Code Values
enum MeHfsError {
    NoError,
    UncategorizedFailure,
    Disabled,
    ImageFailure,
    DebugFailure,
}

enum MeHfsEnforcementPolicy {
    Nothing,
    ShutdownTo,
    ShutdownNow,
    Shutdown_30mins,
}
