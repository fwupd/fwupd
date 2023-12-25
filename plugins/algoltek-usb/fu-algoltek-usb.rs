// Copyright (C) 2023 Ling.Chen <ling.chen@algoltek.com.tw>
// SPDX-License-Identifier: LGPL-2.1+


#[derive(Getters)]
struct AlgoltekProductIdentity {
    header_len: u8,
    header:[char; 8],
    product_name_len:u8,
    product_name:[char;16],
    version_len:u8,
    version:[char;48],
}
