struct AcpiTable {
    signature: 4s
    length: u32le
    revision: u8
    checksum: u8
    oem_id: 6s
    oem_table_id: 8s
    oem_revision: u32be
    _asl_compiler_id: 4s
    _asl_compiler_revision: u32le
}
