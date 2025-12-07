// Copyright 2025 Richard Hughes <richard@hughsie.com>
// SPDX-License-Identifier: LGPL-2.1-or-later

// The options to use for downloading.
// Since: 1.4.5
enum FwupdClientDownloadFlags {
    // No flags set.
    None = 0,
    // Only use peer-to-peer when downloading URIs.
    // Since: 1.9.4
    OnlyP2p = 1 << 0,
}

// The options to use for uploading.
// Since: 1.4.5
enum FwupdClientUploadFlags {
    // No flags set.
    None = 0,
    // Always use multipart/form-data.
    AlwaysMultipart = 1 << 0,
}
