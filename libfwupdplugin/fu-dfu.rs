#[derive(New, Validate, Parse)]
struct DfuFtr {
    release: u16le
    pid: u16le
    vid: u16le
    ver: u16le
    sig: 3s: const=UFD
    len: u8: default=$struct_size
    crc: u32le
}
#[derive(New, Validate, Parse)]
struct DfuseHdr {
    sig: 5s: const=DfuSe
    ver: u8: const=0x01
    image_size: u32le
    targets: u8
}
#[derive(New, Validate, Parse)]
struct DfuseImage {
    sig: 6s: const=Target
    alt_setting: u8
    target_named: u32le
    target_name: 255s
    target_size: u32le
    chunks: u32le
}
#[derive(New, Validate, Parse)]
struct DfuseElement {
    address: u32le
    size: u32le
}
