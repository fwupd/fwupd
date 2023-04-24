#[derive(ToString)]
enum UsiDockSpiState {
    None,
    SwitchSuccess,
    SwitchFail,
    CmdSuccess,
    CmdFail,
    RwSuccess,
    RwFail,
    Ready,
    Busy,
    Timeout,
    FlashFound,
    FlashNotFound,
}
