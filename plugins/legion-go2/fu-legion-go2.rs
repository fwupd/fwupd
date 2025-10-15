
#[repr(u8)]
enum FuLegionGo2UpgradeStep {
        Start = 0x50,
        QuerySize,
        WriteData,
        Verify,
}

#[repr(u8)]
enum FuLegionGo2ResponseStatus {
        Ok = 0x00,
        Fail,
        Busy,
}

#[derive(New, Getters, Setters, Default, ParseStream)]
#[repr(C, packed)]
struct FuStructLegionGo2UpgradeCmd {
        report_id: u8,
        length: u8,
        main_id: u8 == 0x53,
        sub_id: u8 == 0x11,
        device_id: u8,
        param: u8,
        data: [u8; 58],
}

#[derive(New, Getters, Setters, Default, ParseStream)]
#[repr(C, packed)]
struct FuStructLegionGo2NormalCmd {
        report_id: u8,
        length: u8,
        main_id: u8,
        sub_id: u8,
        device_id: u8,
        data: [u8; 59],
}