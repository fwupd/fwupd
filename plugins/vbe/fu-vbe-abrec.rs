// Copyright 2026 Simon Glass <sjg@chromium.org>
// SPDX-License-Identifier: LGPL-2.1-or-later

// a copy of the firmware, in the order of the pick field of the state
#[derive(ToString, FromString)]
enum FuVbeAbrecSlot {
    A,
    B,
    Recovery,
    Unknown,
}

// the result of the last try of B, as in the state
#[derive(ToString)]
enum FuVbeAbrecResult {
    Unknown,
    Trying,
    Ok,
    Bad,
}
