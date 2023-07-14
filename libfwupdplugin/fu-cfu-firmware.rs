// Copyright (C) 2023 Richard Hughes <richard@hughsie.com>
// SPDX-License-Identifier: LGPL-2.1+

#[derive(New, Validate, Parse)]
struct CfuPayload {
    addr: u32le,
    size: u8,
}

#[repr(u8)]
enum CfuOfferComponentId {
    NotUsed = 0,
    // values in between are either valid or reserved
    OfferInformation = 0xFF,
    OfferInformation2 = 0xFE,
}

#[derive(New, Validate, Parse)]
struct CfuOffer {
    segment_number: u8,
    flags1: u8,
    component_id: CfuOfferComponentId,
    token: u8,
    version: u32le,
    compat_variant_mask: u32le,
    flags2: u8,
    flags3: u8,
    product_id: u16le,
}
