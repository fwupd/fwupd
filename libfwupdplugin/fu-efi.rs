#[derive(New, Validate, Parse)]
struct EfiFile {
    name: guid,
    hdr_checksum: u8,
    data_checksum: u8,
    type: u8,
    attrs: u8,
    size: u24le,
    state: u8: const=0xF8,
}
#[derive(New, Validate, Parse)]
struct EfiSection {
    size: u24le,
    type: u8,
}
#[derive(New, Validate, Parse)]
struct EfiSectionGuidDefined {
    name: guid,
    offset: u16le,
    attr: u16le,
}
#[derive(New, Validate, Parse)]
struct EfiVolume {
    zero_vector: guid,
    guid: guid,
    length: u64le,
    signature: u32le: const=0x4856465F,
    attrs: u32le,
    hdr_len: u16le,
    checksum: u16le,
    ext_hdr: u16le,
    reserved: u8,
    revision: u8: const=0x02,
}
#[derive(New, Validate, Parse)]
struct EfiVolumeBlockMap {
    num_blocks: u32le,
    length: u32le,
}
#[derive(New, Validate, Parse)]
struct EfiSignatureList {
    type: guid,
    list_size: u32le,
    header_size: u32le,
    size: u32le,
}
