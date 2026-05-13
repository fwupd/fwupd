// Copyright 2026 Richard Hughes <richard@hughsie.com>
// SPDX-License-Identifier: LGPL-2.1-or-later

#[derive(ToString)]
enum FuProtobufWireType {
    Varint,     // int32, sint64, bool, enum, etc
    Int64,      // fixed64, sfixed64, double
    Len,        // string, bytes, etc
    StartGroup, // deprecated
    EndGroup,   // deprecated
    Int32,      // fixed32, sfixed32, float
}
