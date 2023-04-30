#[derive(New, Validate, Parse)]
struct SmbiosEp32 {
    anchor_str: 4s,
    entry_point_csum: u8,
    entry_point_len: u8,
    smbios_major_ver: u8,
    smbios_minor_ver: u8,
    max_structure_sz: u16le,
    entry_point_rev: u8,
    _formatted_area: 5u8,
    intermediate_anchor_str: 5s,
    intermediate_csum: u8,
    structure_table_len: u16le,
    structure_table_addr: u32le,
    number_smbios_structs: u16le,
    smbios_bcd_rev: u8,
}
#[derive(New, Validate, Parse)]
struct SmbiosEp64 {
    anchor_str: 5s,
    entry_point_csum: u8,
    entry_point_len: u8,
    smbios_major_ver: u8,
    smbios_minor_ver: u8,
    smbios_docrev: u8,
    entry_point_rev: u8,
    reserved0: u8,
    structure_table_len: u32le,
    structure_table_addr: u64le,
}
#[derive(New, Validate, Parse)]
struct SmbiosStructure {
    type: u8,
    length: u8,
    handle: u16le,
}
