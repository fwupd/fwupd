// Copyright (C) 2023 Adam.Chen <Adam.Chen@genesyslogic.com.tw>
// SPDX-License-Identifier: LGPL-2.1+

// Tool String Descriptor
#[repr(u8)]
#[derive(ToString)]
enum GenesysTsVersion {
    Dynamic_9Byte = 0x30,
    Bonding,
    BondingQc,
    VendorSupport,
    MultiToken,
    Dynamic_2nd,
    Reserved,
    Dynamic_13Byte,
    ProductProject,
}
#[derive(ToString, Parse, New)]
struct GenesysTsStatic {
    tool_string_version: GenesysTsVersion,

    mask_project_code: [char; 4],
    mask_project_hardware: char,      // 0=a, 1=b...
    mask_project_firmware: [char; 2], // 01,02,03...
    mask_project_ic_type: [char; 6],  // 352310=GL3523-10 (ASCII string)

    running_project_code: [char; 4],
    running_project_hardware: char,
    running_project_firmware: [char; 2],
    running_project_ic_type: [char; 6],

    firmware_version: [char; 4], // MMmm=MM.mm (ASCII string)
}

#[derive(ToString, Parse)]
struct GenesysTsDynamicGl3523 {
    running_mode: char, // 'M' for mask code, the others for bank code

    ss_port_number: char, // super-speed port number
    hs_port_number: char, // high-speed port number

    ss_connection_status: char, // bit field. ON = DFP is a super-speed device
    hs_connection_status: char, // bit field. ON = DFP is a high-speed device
    fs_connection_status: char, // bit field. ON = DFP is a full-speed device
    ls_connection_status: char, // bit field. ON = DFP is a low-speed device

    charging: char,                  // bit field. ON = DFP is a charging port
    non_removable_port_status: char, // bit field. ON = DFP is a non-removable port

    //   Bonding reports Hardware register status for GL3523: (ASCII)
    //     2 / 4 ports         : 1 means 4 ports, 0 means 2 ports
    //     MTT / STT           : 1 means Multi Token Transfer, 0 means Single TT
    //     Type - C            : 1 means disable, 0 means enable
    //     QC                  : 1 means disable, 0 means enable
    //     Flash dump location : 1 means 32KB offset bank 1, 0 means 0 offset bank 0.

    //   Tool string Version 1:
    //     Bit3 : Flash dump location
    //     BIT2 : Type - C
    //     BIT1 : MTT / STT
    //     BIT0 : 2 / 4 ports

    //   Tool string Version 2 or newer :
    //     Bit4 : Flash dump location
    //     BIT3 : Type - C
    //     BIT2 : MTT / STT
    //     BIT1 : 2 / 4 ports
    //     BIT0 : QC
    //   Default use '0'~'F', plus Bit4 may over value, should extract that.
    bonding: char,
}

#[derive(ToString, Parse)]
struct GenesysTsDynamicGl3590 {
    running_mode: char,

    ss_port_number: char,
    hs_port_number: char,

    ss_connection_status: char,
    hs_connection_status: char,
    fs_connection_status: char,
    ls_connection_status: char,

    charging: char,
    non_removable_port_status: char,

    //   Bonding for GL3590-10/20:
    //     Bit7 : Flash dump location, 0 means bank 0, 1 means bank 1.
    bonding: u8,
}

#[derive(ToString)]
#[repr(u8)]
enum GenesysFwStatus {
    Mask = 0x30,
    Bank1,
    Bank2,
}
#[derive(ToString, Parse)]
struct GenesysTsDynamicGl359030 {
    running_mode: char,

    ss_port_number: char,
    hs_port_number: char,

    ss_connection_status: char,
    hs_connection_status: char,
    fs_connection_status: char,
    ls_connection_status: char,

    charging: char,
    non_removable_port_status: char,

    bonding: u8,

    hub_fw_status: GenesysFwStatus,
    dev_fw_status: GenesysFwStatus,
    dev_fw_version: u16le,
}

#[derive(ToString, Parse)]
struct GenesysTsDynamicGl3525 {
    running_mode: char,

    ss_port_number: char,
    hs_port_number: char,

    ss_connection_status: char,
    hs_connection_status: char,
    fs_connection_status: char,
    ls_connection_status: char,

    charging: char,
    non_removable_port_status: char,

    bonding: u8,

    hub_fw_status: GenesysFwStatus,
    pd_fw_status: GenesysFwStatus,
    pd_fw_version: u16le,
    dev_fw_status: GenesysFwStatus,
    dev_fw_version: u16le,
}

#[derive(ToString, Parse)]
struct GenesysTsFirmwareInfo {
    tool_version: [u8; 6],      // ISP tool defined by itself
    address_mode: u8,           // 3 or 4: support 3 or 4-bytes address, others are no meaning.
    build_fw_time: [char; 12],  // YYYYMMDDhhmm
    update_fw_time: [char; 12], // YYYYMMDDhhmm
}

#[derive(ToString, New, Parse)]
struct GenesysTsVendorSupport {
    version: [char; 2],
    supports: [char; 29],
}

// Firmware info
#[derive(ToString)]
enum GenesysFwCodesign {
    None,
    Rsa,
    Ecdsa,
}

#[derive(Getters, Validate)]
struct GenesysFwCodesignInfoRsa {
    tag_n: u32be: const=0x4E203D20, // 'N = '
    text_n: [char; 512],
    end_n: u16be: const=0x0D0A,
    tag_e: u32be: const=0x45203D20, // 'E = '
    text_e: [char; 6],
    end_e: u16be: const=0x0D0A,
    signature: [u8; 256],
}
#[derive(Parse, Validate)]
struct GenesysFwRsaPublicKeyText {
    tag_n: u32be: const=0x4E203D20, // 'N = '
    text_n: [char; 512],
    end_n: u16be: const=0x0D0A,
    tag_e: u32be: const=0x45203D20, // 'E = '
    text_e: [char; 6],
    end_e: u16be: const=0x0D0A,
}

#[derive(Getters, Validate)]
struct GenesysFwCodesignInfoEcdsa {
    hash: [u8; 32],
    key: [u8; 64],
    signature: [u8; 64],
}
#[derive(Parse, Validate)]
struct GenesysFwEcdsaPublicKey {
    key: [u8; 64],
}

#[derive(ToString)]
enum GenesysFwType {
    Hub, // inside hub start
    DevBridge,
    Pd,
    Codesign, // inside hub end
    InsideHubCount,

    Scaler, // vendor support start

    Unknown = 0xff,
}
