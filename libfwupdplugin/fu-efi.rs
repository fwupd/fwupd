// Copyright 2023 Richard Hughes <richard@hughsie.com>
// SPDX-License-Identifier: LGPL-2.1-or-later

#[derive(ToString, FromString)]
enum FuEfiSignatureKind {
    Unknown,
    Sha256,
    X509,
}

#[repr(u8)]
enum FuEfiFileAttrib {
    None = 0x00,
    LargeFile = 0x01,
    DataAlignment2 = 0x02,
    Fixed = 0x04,
    DataAlignment = 0x38,
    Checksum = 0x40,
}

#[repr(u8)]
#[derive(ToString)]
enum FuEfiFileType {
    All = 0x00,
    Raw = 0x01,
    Freeform = 0x02,
    SecurityCore = 0x03,
    PeiCore = 0x04,
    DxeCore = 0x05,
    Peim = 0x06,
    Driver = 0x07,
    CombinedPeimDriver = 0x08,
    Application = 0x09,
    Mm = 0x0A,
    FirmwareVolumeImage = 0x0B,
    CombinedMmDxe = 0x0C,
    MmCore = 0x0D,
    MmStandalone = 0x0E,
    MmCoreStandalone = 0x0F,
    FfsPad = 0xF0,
}

#[derive(New, Validate, ParseStream, Default)]
#[repr(C, packed)]
struct FuStructEfiFile {
    name: Guid,
    hdr_checksum: u8,
    data_checksum: u8,
    type: FuEfiFileType,
    attrs: u8,
    size: u24le,
    state: u8 = 0xF8,
}

#[derive(ParseStream)]
#[repr(C, packed)]
struct FuStructEfiFile2 {
    _base: FuStructEfiFile,
    extended_size: u64le,
}

#[repr(u8)]
enum FuEfiCompressionType {
    NotCompressed = 0x00,
    StandardCompression = 0x01,
}

#[derive(ParseStream)]
#[repr(C, packed)]
struct FuStructEfiSectionCompression {
    uncompressed_length: u32le,
    compression_type: FuEfiCompressionType,
}

#[derive(ToString)]
enum FuEfiLz77DecompressorVersion {
    None,
    Legacy,
    Tiano,
}

#[derive(ParseStream)]
#[repr(C, packed)]
struct FuStructEfiLz77DecompressorHeader {
    src_size: u32le,
    dst_size: u32le,
}

#[repr(u8)]
#[derive(ToString)]
enum FuEfiSectionType {
    // encapsulation section type values
    Compression = 0x01,
    GuidDefined = 0x02,
    Disposable = 0x03,
    // leaf section type values
    Pe32 = 0x10,
    Pic = 0x11,
    Te = 0x12,
    DxeDepex = 0x13,
    Version = 0x14,
    UserInterface = 0x15,
    Compatibility16 = 0x16,
    VolumeImage = 0x17,
    FreeformSubtypeGuid = 0x18,
    Raw = 0x19,
    PeiDepex = 0x1B,
    MmDepex = 0x1C,
    // vendor-specific
    PhoenixSectionPostcode = 0xF0,  // Phoenix SCT
    InsydeSectionPostcode = 0x20,   // Insyde H2O
}

#[derive(New, ParseStream)]
#[repr(C, packed)]
struct FuStructEfiSection {
    size: u24le,
    type: FuEfiSectionType,
}

#[derive(ParseStream)]
#[repr(C, packed)]
struct FuStructEfiSection2 {
    _base: FuStructEfiSection,
    extended_size: u32le,
}

#[derive(ParseStream)]
#[repr(C, packed)]
struct FuStructEfiSectionFreeformSubtypeGuid {
    guid: Guid,
}

#[derive(New, ParseStream)]
#[repr(C, packed)]
struct FuStructEfiSectionGuidDefined {
    name: Guid,
    offset: u16le,
    attr: u16le,
}

#[derive(New, ValidateStream, ParseStream, Default)]
#[repr(C, packed)]
struct FuStructEfiVolume {
    zero_vector: Guid,
    guid: Guid,
    length: u64le,
    signature: u32le == 0x4856465F,
    attrs: u32le,
    hdr_len: u16le,
    checksum: u16le,
    ext_hdr: u16le,
    reserved: u8,
    revision: u8 == 0x02,
}

#[derive(ParseStream)]
#[repr(C, packed)]
struct FuStructEfiVolumeExtHeader {
    fv_name: Guid,
    size: u32le,
}

#[repr(u16le)]
#[derive(ToString)]
enum FuEfiVolumeExtEntryType {
    Oem = 0x01,
    Guid = 0x02,
    Size = 0x03,
}

#[derive(ParseStream)]
#[repr(C, packed)]
struct FuStructEfiVolumeExtEntry {
    size: u16le,
    type: FuEfiVolumeExtEntryType,
}

#[derive(New, ParseStream)]
#[repr(C, packed)]
struct FuStructEfiVolumeBlockMap {
    num_blocks: u32le,
    length: u32le,
}

#[derive(New, ParseStream)]
#[repr(C, packed)]
struct FuStructEfiSignatureList {
    type: Guid,
    list_size: u32le,
    header_size: u32le,
    size: u32le,
}

#[repr(u32le)]
enum FuEfiLoadOptionAttrs {
    Active = 0x1,
    ForceReconnect = 0x2,
    Hidden = 0x8,
    Category = 0x1F00,
    CategoryBoot = 0x0,
    CategoryAp = 0x100,
}

#[derive(ToString, FromString)]
enum FuEfiLoadOptionKind {
    Unknown,
    Path,
    Hive,
    Data,
}

#[derive(ParseStream, New)]
#[repr(C, packed)]
struct FuStructEfiLoadOption {
    attrs: FuEfiLoadOptionAttrs,
    dp_size: u16le,
}

#[repr(u8)]
enum FuEfiDevicePathType {
    Hardware = 0x01,
    Acpi,
    Message,
    Media,
    BiosBoot,
    End = 0x7F,
}

#[derive(ParseStream, New, Default)]
#[repr(C, packed)]
struct FuStructEfiDevicePath {
    type: FuEfiDevicePathType,
    subtype: u8 = 0xFF,
    length: u16le = $struct_size,
}

#[repr(u8)]
enum FuEfiHardDriveDevicePathSubtype {
    HardDrive = 0x01,
    Cdrom = 0x02,
    Vendor = 0x03,
    FilePath = 0x04,
    MediaProtocol = 0x05,
    PiwgFirmwareFile = 0x06,
    PiwgFirmwareVolume = 0x07,
    RelativeOffsetRange = 0x08,
    RamDiskDevicePath = 0x09,
}

#[repr(u8)]
#[derive(ToString, FromString)]
enum FuEfiHardDriveDevicePathPartitionFormat {
    LegacyMbr = 0x01,
    GuidPartitionTable = 0x02,
}

#[repr(u8)]
#[derive(ToString, FromString)]
enum FuEfiHardDriveDevicePathSignatureType {
    None,
    Addr1b8,
    Guid,
}

#[derive(ParseStream, New, Default)]
#[repr(C, packed)]
struct FuStructEfiHardDriveDevicePath {
    type: FuEfiDevicePathType == Media,
    subtype: FuEfiHardDriveDevicePathSubtype = HardDrive,
    length: u16le == $struct_size,
    partition_number: u32le,
    partition_start: u64le,
    partition_size: u64le,
    partition_signature: Guid,
    partition_format: FuEfiHardDriveDevicePathPartitionFormat = GuidPartitionTable,
    signature_type: FuEfiHardDriveDevicePathSignatureType = Guid,
}

#[derive(ParseStream, New, Default)]
#[repr(C, packed)]
struct FuStructShimHive {
    magic: [char; 4] == "HIVE",
    header_version: u8 = 0x1,
    items_count: u8,
    items_offset: u8,    // for forwards and backwards compatibility
    crc32: u32le,        // of the entire hive (excluding padding)
    //items: [ShimHiveItems; items_count]
}

#[derive(ParseStream, New)]
#[repr(C, packed)]
struct FuStructShimHiveItem {
    key_length: u8,
    value_length: u32le,
    // key string, no trailing NUL
    // value string, no trailing NUL
}
