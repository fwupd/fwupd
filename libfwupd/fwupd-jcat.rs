// Copyright 2026 Richard Hughes <richard@hughsie.com>
// SPDX-License-Identifier: LGPL-2.1-or-later

// Since: 2.1.3
#[derive(ToString, FromString)]
enum FwupdJcatBlobKind {
    Unknown,
    Sha256,
    Gpg,
    Pkcs7,
    Sha1,
    BtManifest,
    BtCheckpoint,
    BtInclusionProof,
    BtVerifier,
    Ed25519,
    Sha512,
    BtLogindex,
}

// Since: 2.1.3
#[derive(ToString)]
enum FwupdJcatBlobMethod {
    Unknown,
    Checksum,
    Signature,
}

// Since: 2.1.3
#[derive(ToString)]
enum FwupdJcatBlobFlags {
    None = 0,
    IsUtf8 = 1 << 0,
}
