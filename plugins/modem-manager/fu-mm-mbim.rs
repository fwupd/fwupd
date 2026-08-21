/*
 * Copyright 2024 TDT AG <development@tdt.de>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#[derive(ToString, FromString)]
enum FuMmMbimDetachMethod {
    Unknown,
    QduQuectel,
    Fibocom,
}
