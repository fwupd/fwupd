package org.freedesktop.fwupd;

@VintfStability
parcelable FwupdUpdate {
    String remoteId;
    String name;
    String version;
    String filename;
    @nullable String appstreamId;
    @nullable String releaseId;
    @nullable String nameVariantSuffix;
    @nullable String summary;
    @nullable String description;
    @nullable String branch;
    @nullable String protocol;
    @nullable String[] categories;
    @nullable String[] issues;
    @nullable String checksum;
    @nullable String[] tags;
    @nullable String license;
    @nullable String[] locations;
    @nullable String homepage;
    @nullable String detailsUrl;
    @nullable String sourceUrl;
    @nullable String sbomUrl;
    @nullable String vendor;
    @nullable String detachCaption;
    @nullable String detachImage;
    @nullable String updateMessage;
    @nullable String updateImage;
    long size;
    long created;
    long flags;
    long trustFlags;
    int urgency;
    int installDuration;
}
