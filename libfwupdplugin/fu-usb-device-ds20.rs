// Copyright 2023 Richard Hughes <richard@hughsie.com>
// SPDX-License-Identifier: LGPL-2.1-or-later

#[derive(New, ParseStream)]
#[repr(C, packed)]
struct FuStructDs20 {
    _reserved: u8,
    guid: Guid,
    platform_ver: u32le,
    total_length: u16le,
    vendor_code: u8,
    alt_code: u8,
}

#[derive(New, ParseStream)]
#[repr(C, packed)]
struct FuStructMsDs20 {
    size: u16le,
    type: u16le,
}

#[derive(ToString)]
enum FuUsbDeviceMsDs20Desc {
    SetHeaderDescriptor,
    SubsetHeaderConfiguration,
    SubsetHeaderFunction,
    FeatureCompatibleId,
    FeatureRegProperty,
    FeatureMinResumeTime,
    FeatureModelId,
    FeatureCcgpDevice,
    FeatureVendorRevision,
}
