package org.freedesktop.fwupd;

@VintfStability
parcelable FwupdRequest {
    String id;
    int kind;
    @nullable String message;
}