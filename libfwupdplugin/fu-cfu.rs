#[derive(New, Validate, Parse)]
struct CfuPayload {
    addr: u32le,
    size: u8,
}
#[derive(New, Validate, Parse)]
struct CfuOffer {
    segment_number: u8,
    flags1: u8,
    component_id: u8,
    token: u8,
    version: u32le,
    compat_variant_mask: u32le,
    flags2: u8,
    flags3: u8,
    product_id: u16le,
}
