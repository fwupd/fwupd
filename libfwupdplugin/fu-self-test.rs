// Copyright (C) 2023 Richard Hughes <richard@hughsie.com>
// SPDX-License-Identifier: LGPL-2.1+

#[repr(u8)]
enum SelfTestRevision {
    None = 0x0,
    All	= 0xF_F,
}

#[derive(New, Validate, Parse, ToString)]
struct SelfTest {
    signature: u32be == 0x1234_5678,
    length: u32le = $struct_size, // bytes
    revision: SelfTestRevision,
    owner: Guid,
    oem_id: [char; 6] == "ABCDEF",
    oem_table_id: [char; 8],
    oem_revision: u32le,
    asl_compiler_id: [u8; 4] =	0xDF,
    asl_compiler_revision: u32le,
}

#[derive(New, Validate, Parse, ToString)]
struct SelfTestWrapped {
    less: u8,
    base: SelfTest,
    more: u8,
}

#[repr(u4)]
enum FuStructSelfTestLower {
    None = 0x0,
    One = 0x1,
    Two = 0x2,
}

#[derive(New, Parse, ToString)]
struct FuStructSelfTestBits {
    lower: FuStructSelfTestLower = Two,
    middle: u1 = 0b1,
    upper: u4 = 0xF,
}
