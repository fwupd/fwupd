// Copyright 2024 Lars erling stensen <Lars.erling.stensen@huddly.com>
// SPDX-License-Identifier: LGPL-2.1-or-later

#[derive(New, Parse)]
struct FuStructHLinkHeader {
    req_id: u32le,
    res_id: u32le,
    flags: u16le,
    msg_name_size: u16le,
    payload_size: u32le,
}
