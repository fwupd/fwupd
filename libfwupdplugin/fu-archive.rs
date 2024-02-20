// Copyright (C) 2023 Richard Hughes <richard@hughsie.com>
// SPDX-License-Identifier: LGPL-2.1+

#[derive(FromString, ToString)]
enum FuArchiveFormat {
    Unknown, // Since: 1.8.1
    Cpio,    // Since: 1.8.1
    Shar,    // Since: 1.8.1
    Tar,     // Since: 1.8.1
    Ustar,   // Since: 1.8.1
    Pax,     // Since: 1.8.1
    Gnutar,  // Since: 1.8.1
    Iso9660, // Since: 1.8.1
    Zip,     // Since: 1.8.1
    Ar,      // Since: 1.8.1
    ArSvr4,  // Since: 1.8.1
    Mtree,   // Since: 1.8.1
    Raw,     // Since: 1.8.1
    Xar,     // Since: 1.8.1
    7zip,    // Since: 1.8.1
    Warc,    // Since: 1.8.1
}

#[derive(FromString, ToString)]
enum FuArchiveCompression {
    Unknown,  // Since: 1.8.1
    None,     // Since: 1.8.1
    Gzip,     // Since: 1.8.1
    Bzip2,    // Since: 1.8.1
    Compress, // Since: 1.8.1
    Lzma,     // Since: 1.8.1
    Xz,       // Since: 1.8.1
    Uu,       // Since: 1.8.1
    Lzip,     // Since: 1.8.1
    Lrzip,    // Since: 1.8.1
    Lzop,     // Since: 1.8.1
    Grzip,    // Since: 1.8.1
    Lz4,      // Since: 1.8.1
    Zstd,     // Since: 1.8.1
}
