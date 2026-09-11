package org.freedesktop.fwupd;

@VintfStability
parcelable FwupdSecurityAttr {
    @nullable String appstreamId;
    @nullable String name;
    @nullable String title;
    @nullable String description;
    @nullable String plugin;
    @nullable String url;
    @nullable String fwupdVersion;
    @nullable String biosSettingId;
    @nullable String biosSettingCurrentValue;
    @nullable String biosSettingTargetValue;
    @nullable String kernelCurrentValue;
    @nullable String kernelTargetValue;
    @nullable String[] guids;
    @nullable String[] obsoletes;
    int level;
    int result;
    int resultFallback;
    int resultSuccess;
    long flags;
    long created;
}
