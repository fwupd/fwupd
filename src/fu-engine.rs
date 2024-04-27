// Copyright (C) 2023 Richard Hughes <richard@hughsie.com>
// SPDX-License-Identifier: LGPL-2.1+

#[derive(FromString)]
enum ReleasePriority {
    None,
    Local,
    Remote,
}

#[derive(FromString)]
enum P2pPolicy {
    Nothing = 0x00,
    Metadata = 0x01,
    Firmware = 0x02,
}

#[derive(ToString)]
enum EngineInstallPhase {
    Setup,
    Install,
    Attach,
    Detach,
    Prepare,
    Cleanup,
    Reload,
    CompositePrepare,
    CompositeCleanup,
}

#[derive(ToBitString)]
enum EngineRequestFlag {
    None = 0,
    NoRequirements = 1 << 0,
    AnyRelease = 1 << 1,
}

#[derive(ToBitString)]
enum IdleInhibit {
    None = 0,
    Timeout = 1 << 0,
    Signals = 1 << 1,
}

enum ClientFlag {
    None = 0,
    Active = 1 << 0,
}
