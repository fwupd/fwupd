// Copyright 2023 Richard Hughes <richard@hughsie.com>
// SPDX-License-Identifier: LGPL-2.1-or-later

#[derive(New, ParseStream)]
#[repr(C, packed)]
struct FuStructRedfishProtocolOverIp {
    service_uuid: Guid,
    host_ip_assignment_type: u8,
    host_ip_address_format: u8,
    host_ip_address: [u8; 16],
    host_ip_mask: [u8; 16],
    service_ip_assignment_type: u8,
    service_ip_address_format: u8,
    service_ip_address: [u8; 16],
    service_ip_mask: [u8; 16],
    service_ip_port: u16le,
    service_ip_vlan_id: u32le,
    service_hostname_len: u8,
// optional service_hostname goes here
}

#[derive(ToString)]
enum FuRedfishInterfaceType {
    UsbNetwork = 0x02,
    PciNetwork = 0x03,
    UsbNetworkV2 = 0x04,
    PciNetworkV2 = 0x05,
}

enum FuRedfishIpAssignmentType {
    Static = 0x00,
    Dhcp = 0x02,
    AutoConfig = 0x03,
    HostSelect = 0x04,
}

enum FuRedfishIpAddressFormat {
    Unknown = 0x00,
    V4 = 0x01,
    V6 = 0x02,
}

#[repr(u8)]
#[derive(ToString)]
enum FuRedfishSmbiosInterfaceType {
    Unknown = 0x00,
    Kcs = 0x02,
    8250Uart = 0x03,
    16450Uart = 0x04,
    16550Uart = 0x05,
    16650Uart = 0x06,
    16750Uart = 0x07,
    16850Uart = 0x08,
    Mctp = 0x3F,
    Network = 0x40,
    Oem = 0xF0,
}

#[derive(ParseStream, Default)]
#[repr(C, packed)]
struct FuStructRedfishSmbiosType42 {
    type: u8 == 42,
    length: u8,
    handle: u16le,
    interface_type: FuRedfishSmbiosInterfaceType = Network,
    data_length: u8,
    // data: [u8; data_length],
    // protocol_cnt: u8,
    // protocol_records
}

#[derive(ToString)]
enum FuRedfishNetworkDeviceState {
    Unknown = 0,
    Unmanaged = 10,
    Unavailable = 20,
    Disconnected = 30,
    Prepare = 40,
    Config = 50,
    NeedAuth = 60,
    IpConfig = 70,
    IpCheck = 80,
    Secondaries = 90,
    Activated = 100,
    Deactivating = 110,
    Failed = 120,
}
