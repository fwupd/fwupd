package org.freedesktop.fwupd;

import org.freedesktop.fwupd.FwupdRelease;

@VintfStability
parcelable FwupdDevice {
    String id;
    String name;
    String version;
    String plugin;
    @nullable String parentId;
    @nullable String compositeId;
    @nullable String[] instanceIds;
    @nullable String[] guid;
    @nullable String serial;
    @nullable String summary;
    @nullable String detailsUrl;
    @nullable String branch;
    @nullable String[] protocols;
    @nullable String[] issues;
    @nullable String[] problems;
    @nullable String[] checksums;
    @nullable String vendor;
    @nullable String[] vendorIds;
    @nullable String versionLowest;
    @nullable String versionHighest;
    @nullable String versionBootloader;
    @nullable String[] icons;
    @nullable String updateError;
    @nullable FwupdRelease[] releases;
    long flags;
    long requestFlags;
    int versionFormat;
    int flashesLeft;
    int batteryLevel;
    int batteryThreshold;
    long versionRaw;
    long versionLowestRaw;
    long versionHighestRaw;
    long versionBootloaderRaw;
    long versionBuildDate;
    int installDuration;
    long created;
    long modified;
    int updateState;
    int status;
    int percentage;
}
