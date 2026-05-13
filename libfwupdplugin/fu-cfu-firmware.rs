// Copyright 2023 Richard Hughes <richard@hughsie.com>
// SPDX-License-Identifier: LGPL-2.1-or-later

#[derive(New, ParseStream)]
#[repr(C, packed)]
struct FuStructCfuPayload {
    addr: u32le,
    size: u8,
}

#[repr(u8)]
enum FuCfuOfferComponentId {
    NotUsed = 0,
    // values in between are either valid or reserved
    OfferInformation = 0xFF,
    OfferInformation2 = 0xFE,
}

#[derive(New, ParseStream)]
#[repr(C, packed)]
struct FuStructCfuOffer {
    segment_number: u8,
    flags1: u8,
    component_id: FuCfuOfferComponentId,
    token: u8,
    version: u32le,
    compat_variant_mask: u32le,
    flags2: u8,
    flags3: u8,
    product_id: u16le,
}
