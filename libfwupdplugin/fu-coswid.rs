// Copyright 2023 Richard Hughes <richard@hughsie.com>
// SPDX-License-Identifier: LGPL-2.1-or-later

#[derive(ToString, FromString)]
enum FuCoswidTag {
    TagId,
    SoftwareName,
    Entity,
    Evidence,
    Link,
    SoftwareMeta,
    Payload,
    Hash,
    Corpus,
    Patch,
    Media,
    Supplemental,
    TagVersion,
    SoftwareVersion,
    VersionScheme,
    Lang,
    Directory,
    File,
    Process,
    Resource,
    Size,
    FileVersion,
    Key,
    Location,
    FsName,
    Root,
    PathElements,
    ProcessName,
    Pid,
    Type,
    Missing30, // not in the spec!
    EntityName,
    RegId,
    Role,
    Thumbprint,
    Date,
    DeviceId,
    Artifact,
    Href,
    Ownership,
    Rel,
    MediaType,
    Use,
    ActivationStatus,
    ChannelType,
    ColloquialVersion,
    Description,
    Edition,
    EntitlementDataRequired,
    EntitlementKey,
    Generator,
    PersistentId,
    Product,
    ProductFamily,
    Revision,
    Summary,
    UnspscCode,
    UnspscVersion,
}

#[derive(ToString, FromString)]
enum FuCoswidVersionScheme {
    Unknown,
    Multipartnumeric,
    MultipartnumericSuffix,
    Alphanumeric,
    Decimal,
    Semver = 16384,
}

#[derive(ToString, FromString)]
enum FuCoswidLinkRel {
    License = -2,
    Compiler = -1,
    Unknown = 0,
    Ancestor = 1,
    Component = 2,
    Feature = 3,
    Installationmedia = 4,
    Packageinstaller = 5,
    Parent = 6,
    Patches = 7,
    Requires = 8,
    SeeAlso = 9,
    Supersedes = 10,
    Supplemental = 11,
}

#[derive(ToString, FromString)]
enum FuCoswidEntityRole {
    Unknown,
    TagCreator,
    SoftwareCreator,
    Aggregator,
    Distributor,
    Licensor,
    Maintainer,
}

#[derive(ToString, FromString)]
enum FuCoswidHashAlg {
    Unknown = 0,
    SHA256 = 1,
    SHA384 = 7,
    SHA512 = 8,
}
