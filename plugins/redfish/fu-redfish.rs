#[derive(New, Parse)]
struct RedfishProtocolOverIp {
    service_uuid: guid,
    host_ip_assignment_type: u8,
    host_ip_address_format: u8,
    host_ip_address: 16u8,
    host_ip_mask: 16u8,
    service_ip_assignment_type: u8,
    service_ip_address_format: u8,
    service_ip_address: 16u8,
    service_ip_mask: 16u8,
    service_ip_port: u16le,
    service_ip_vlan_id: u32le,
    service_hostname_len: u8,
// optional service_hostname goes here
}
#[derive(ToString)]
enum RedfishNetworkDeviceState {
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
