package org.freedesktop.fwupd;

@VintfStability
// This is an api which returns the list of all sources that Fwupd fetches the firmware from. 
// For example, LVFS or any internal repositories.
parcelable FwupdRemote {
    @nullable String id;
    @nullable String kind;
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
    boolean enabled;
    boolean approvalRequired;
    boolean automaticReports;
    boolean automaticSecurityReports;
    int priority;
    long mtime;
    int refreshIntervalSec;
    @nullable String remotesDir;
    @nullable String[] orderAfter;
    @nullable String[] orderBefore;
    @nullable String keyring;
}
