// Copyright 2025 Richard Hughes <richard@hughsie.com>
// SPDX-License-Identifier: LGPL-2.1-or-later

#[derive(ToString)]
enum FuIhexFirmwareRecordType {
    Data,
    Eof,
    ExtendedSegment,
    StartSegment,
    ExtendedLinear,
    StartLinear,
    Signature = 0xFD,
}
