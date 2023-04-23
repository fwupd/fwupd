struct AcpiPhatHealthRecord {
    signature: u16le: default=0x1
    rcdlen: u16le
    version: u8
    reserved: 2u8
    flags: u8
    device_signature: guid
    device_specific_data: u32le
}
struct AcpiPhatVersionElement {
    component_id: guid
    version_value: u64le
    producer_id: 4s
}
struct AcpiPhatVersionRecord {
    signature: u16le: default=0x0
    rcdlen: u16le
    version: u8
    reserved: 3u8
    record_count: u32le
}
