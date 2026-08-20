package org.freedesktop.fwupd;

@VintfStability
parcelable FwupdRequest {
    String id;
    int kind;
    @nullable String deviceId;
    @nullable String message;
    @nullable String image;
    long created;
    long flags;
}