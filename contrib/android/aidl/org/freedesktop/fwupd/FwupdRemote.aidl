package org.freedesktop.fwupd;

@VintfStability
parcelable FwupdRemote {
    String id;
    int kind;
    @nullable String reportUri;
    @nullable String metadataUri;
    @nullable String metadataUriSig;
    @nullable String firmwareBaseUri;
    @nullable String username;
    @nullable String title;
    @nullable String privacyUri;
    @nullable String agreement;
    @nullable String checksum;
    @nullable String checksumSig;
    @nullable String filenameCache;
    @nullable String filenameCacheSig;
    @nullable String filenameSource;
    long flags;
    int priority;
    long mtime;
    int refreshIntervalSec;
    @nullable String remotesDir;
}
